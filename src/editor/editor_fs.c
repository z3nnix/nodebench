#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "editor/editor_fs.h"

static char editor_fs_lower(char c) {
	if (c >= 'A' && c <= 'Z') { return (char)(c - 'A' + 'a'); }
	return c;
}

static int editor_fs_name_ends_with(const char* name, const char* suffix) {
	size_t name_len = strlen(name);
	size_t suffix_len = strlen(suffix);
	if (name_len < suffix_len) { return 0; }
	for (size_t i = 0; i < suffix_len; i++) {
		if (editor_fs_lower(name[name_len - suffix_len + i]) != suffix[i]) { return 0; }
	}
	return 1;
}

static void editor_fs_join(char* out, size_t size, const char* a, const char* b) {
	if (!out || size == 0) { return; }
	size_t len = strlen(a);
	if (len > size - 1) { len = size - 1; }
	memcpy(out, a, len);
	if (len < size - 1) { out[len++] = '/'; }
	size_t avail = size - len - 1;
	snprintf(out + len, avail + 1, "%.*s", (int)avail, b);
}

static int editor_fs_name_compare(const char* a, const char* b) {
	while (*a && *b) {
		char ca = editor_fs_lower(*a);
		char cb = editor_fs_lower(*b);
		if (ca != cb) { return ca < cb ? -1 : 1; }
		a++;
		b++;
	}
	if (*a) { return 1; }
	if (*b) { return -1; }
	return 0;
}

static int editor_fs_entry_compare(const void* lhs, const void* rhs) {
	const editor_fs_entry_t* a = (const editor_fs_entry_t*)lhs;
	const editor_fs_entry_t* b = (const editor_fs_entry_t*)rhs;
	if (a->kind != b->kind) { return a->kind == NB_EDITOR_FS_KIND_DIR ? -1 : 1; }
	return editor_fs_name_compare(a->name, b->name);
}

static int editor_fs_lex_normalize(const char* path, char* out, size_t size) {
	if (!path || !out || size < 2) { return 0; }
	char work[NB_EDITOR_FS_PATH_MAX];
	if (path[0] == '/') {
		snprintf(work, sizeof(work), "%s", path);
	}
	else {
		char cwd[NB_EDITOR_FS_PATH_MAX];
		if (!getcwd(cwd, sizeof(cwd))) { return 0; }
		editor_fs_join(work, sizeof(work), cwd, path);
	}

	char* parts[256];
	int count = 0;
	char* cursor = work;
	while (*cursor) {
		while (*cursor == '/') { cursor++; }
		if (!*cursor) { break; }
		char* token = cursor;
		while (*cursor && *cursor != '/') { cursor++; }
		if (*cursor) { *cursor++ = '\0'; }
		if (strcmp(token, ".") == 0) { continue; }
		if (strcmp(token, "..") == 0) {
			if (count > 0) { count--; }
			continue;
		}
		if (count < (int)(sizeof(parts) / sizeof(parts[0]))) { parts[count++] = token; }
	}

	size_t pos = 0;
	for (int i = 0; i < count; i++) {
		if (pos + 1 >= size) { return 0; }
		out[pos++] = '/';
		size_t len = strlen(parts[i]);
		if (len > size - pos - 1) { len = size - pos - 1; }
		memcpy(out + pos, parts[i], len);
		pos += len;
	}
	if (pos == 0) {
		out[pos++] = '/';
	}
	out[pos] = '\0';
	return 1;
}

int editor_fs_normalize(const char* path, char* out, size_t size) {
	if (!path || !out || size < 2) { return 0; }
	char resolved[NB_EDITOR_FS_PATH_MAX];
	if (realpath(path, resolved)) {
		return editor_fs_lex_normalize(resolved, out, size);
	}

	const char* slash = strrchr(path, '/');
	if (slash && slash != path) {
		size_t parent_len = (size_t)(slash - path);
		char parent[NB_EDITOR_FS_PATH_MAX];
		if (parent_len < sizeof(parent)) {
			memcpy(parent, path, parent_len);
			parent[parent_len] = '\0';
			if (realpath(parent, resolved)) {
				char tmp[NB_EDITOR_FS_PATH_MAX];
				editor_fs_join(tmp, sizeof(tmp), resolved, slash + 1);
				return editor_fs_lex_normalize(tmp, out, size);
			}
		}
	}
	return editor_fs_lex_normalize(path, out, size);
}

int editor_fs_is_inside(const char* root, const char* path) {
	char root_norm[NB_EDITOR_FS_PATH_MAX];
	char path_norm[NB_EDITOR_FS_PATH_MAX];
	if (!editor_fs_normalize(root, root_norm, sizeof(root_norm))) { return 0; }
	if (!editor_fs_normalize(path, path_norm, sizeof(path_norm))) { return 0; }
	if (strcmp(root_norm, "/") == 0) { return 1; }
	if (strcmp(root_norm, path_norm) == 0) { return 1; }
	size_t len = strlen(root_norm);
	return strncmp(path_norm, root_norm, len) == 0 && path_norm[len] == '/';
}

int editor_fs_exists(const char* path) {
	struct stat st;
	return path && stat(path, &st) == 0;
}

int editor_fs_is_dir(const char* path) {
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int editor_fs_list_dir(const char* dir, editor_fs_entry_t* out, int cap, int show_hidden) {
	if (!dir || !out || cap <= 0) { return 0; }
	DIR* handle = opendir(dir);
	if (!handle) { return 0; }
	int count = 0;
	struct dirent* item = NULL;
	while ((item = readdir(handle)) != NULL && count < cap) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) { continue; }
		if (editor_fs_name_ends_with(item->d_name, ".meta")) { continue; }
		if (!show_hidden && item->d_name[0] == '.') { continue; }
		char path[NB_EDITOR_FS_PATH_MAX];
		editor_fs_join(path, sizeof(path), dir, item->d_name);
		struct stat st;
		if (stat(path, &st) != 0) { continue; }
		editor_fs_entry_t* entry = &out[count];
		entry->kind = S_ISDIR(st.st_mode) ? NB_EDITOR_FS_KIND_DIR : S_ISREG(st.st_mode) ? NB_EDITOR_FS_KIND_FILE : NB_EDITOR_FS_KIND_OTHER;
		entry->size = (long)st.st_size;
		snprintf(entry->name, sizeof(entry->name), "%.*s", (int)sizeof(entry->name) - 1, item->d_name);
		snprintf(entry->path, sizeof(entry->path), "%.*s", (int)sizeof(entry->path) - 1, path);
		count++;
	}
	closedir(handle);
	qsort(out, (size_t)count, sizeof(editor_fs_entry_t), editor_fs_entry_compare);
	return count;
}

int editor_fs_make_dir(const char* path) {
	if (!path || !*path) { return 0; }
	if (editor_fs_is_dir(path)) { return 1; }
	if (mkdir(path, 0755) == 0) { return 1; }
	if (errno != ENOENT) { return 0; }
	char parent[NB_EDITOR_FS_PATH_MAX];
	editor_fs_dirname(path, parent, sizeof(parent));
	if (strcmp(parent, path) == 0 || !*parent) { return 0; }
	if (!editor_fs_make_dir(parent)) { return 0; }
	return mkdir(path, 0755) == 0;
}

int editor_fs_copy_file(const char* src, const char* dst) {
	if (!src || !dst) { return 0; }
	FILE* in = fopen(src, "rb");
	if (!in) { return 0; }
	FILE* out = fopen(dst, "wb");
	if (!out) {
		fclose(in);
		return 0;
	}
	char buffer[8192];
	size_t read_count = 0;
	while ((read_count = fread(buffer, 1, sizeof(buffer), in)) > 0) {
		if (fwrite(buffer, 1, read_count, out) != read_count) {
			fclose(in);
			fclose(out);
			return 0;
		}
	}
	int ok = ferror(in) == 0 && ferror(out) == 0;
	fclose(in);
	fclose(out);
	return ok;
}

int editor_fs_copy_path(const char* src, const char* dst) {
	if (!src || !dst || strcmp(src, dst) == 0 || !editor_fs_exists(src)) { return 0; }
	if (!editor_fs_is_dir(src)) {
		if (editor_fs_exists(dst) && !editor_fs_delete_path(dst)) { return 0; }
		return editor_fs_copy_file(src, dst);
	}
	if (!editor_fs_make_dir(dst)) { return 0; }
	DIR* handle = opendir(src);
	if (!handle) { return 0; }
	struct dirent* item = NULL;
	while ((item = readdir(handle)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) { continue; }
		char from[NB_EDITOR_FS_PATH_MAX];
		char to[NB_EDITOR_FS_PATH_MAX];
		editor_fs_join(from, sizeof(from), src, item->d_name);
		editor_fs_join(to, sizeof(to), dst, item->d_name);
		editor_fs_copy_path(from, to);
	}
	closedir(handle);
	return 1;
}

int editor_fs_move_path(const char* src, const char* dst) {
	if (!src || !dst || strcmp(src, dst) == 0) { return 0; }
	if (!editor_fs_exists(src)) { return 0; }
	if (editor_fs_exists(dst)) {
		if (!editor_fs_delete_path(dst)) { return 0; }
	}
	if (rename(src, dst) == 0) { return 1; }
	if (!editor_fs_is_dir(src)) {
		if (!editor_fs_copy_file(src, dst)) { return 0; }
		return editor_fs_delete_path(src);
	}
	return 0;
}

int editor_fs_delete_path(const char* path) {
	if (!path || !*path) { return 0; }
	struct stat st;
	if (stat(path, &st) != 0) { return 0; }
	if (!S_ISDIR(st.st_mode)) { return unlink(path) == 0; }
	DIR* handle = opendir(path);
	if (!handle) { return 0; }
	struct dirent* item = NULL;
	while ((item = readdir(handle)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) { continue; }
		char child[NB_EDITOR_FS_PATH_MAX];
		editor_fs_join(child, sizeof(child), path, item->d_name);
		editor_fs_delete_path(child);
	}
	closedir(handle);
	return rmdir(path) == 0;
}

int editor_fs_read_text(const char* path, char** out, size_t* out_size) {
	if (!path || !out) { return 0; }
	FILE* file = fopen(path, "rb");
	if (!file) { return 0; }
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return 0;
	}
	long length = ftell(file);
	if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return 0;
	}
	char* data = (char*)malloc((size_t)length + 1);
	if (!data) {
		fclose(file);
		return 0;
	}
	size_t read_count = fread(data, 1, (size_t)length, file);
	int ok = ferror(file) == 0;
	fclose(file);
	if (!ok) {
		free(data);
		return 0;
	}
	data[read_count] = '\0';
	*out = data;
	if (out_size) { *out_size = read_count; }
	return 1;
}

int editor_fs_write_text(const char* path, const char* data, size_t size) {
	if (!path || !data) { return 0; }
	FILE* file = fopen(path, "wb");
	if (!file) { return 0; }
	size_t written = fwrite(data, 1, size, file);
	int ok = written == size && ferror(file) == 0;
	fclose(file);
	return ok;
}

static int editor_fs_ext_equals(const char* path, const char* ext) {
	const char* actual = editor_fs_ext(path);
	while (*actual && *ext) {
		if (editor_fs_lower(*actual) != *ext) { return 0; }
		actual++;
		ext++;
	}
	return *actual == '\0' && *ext == '\0';
}

int editor_fs_is_external_text(const char* path) {
	if (!path || !*path || editor_fs_is_dir(path)) { return 0; }
	const char* internal_exts[] = { "tscn", "tres", "clut", "wav", "obj", "ppm", "meta" };
	for (size_t i = 0; i < sizeof(internal_exts) / sizeof(internal_exts[0]); i++) {
		if (editor_fs_ext_equals(path, internal_exts[i])) { return 0; }
	}
	FILE* file = fopen(path, "rb");
	if (!file) { return 0; }
	unsigned char buffer[4096];
	size_t count = fread(buffer, 1, sizeof(buffer), file);
	int ok = ferror(file) == 0;
	fclose(file);
	if (!ok) { return 0; }
	size_t control = 0;
	for (size_t i = 0; i < count; i++) {
		unsigned char c = buffer[i];
		if (c == 0) { return 0; }
		if (c < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != '\f' && c != '\b') { control++; }
	}
	return count == 0 || control * 20 <= count;
}

static int editor_fs_executable_exists(const char* name) {
	if (strchr(name, '/')) { return access(name, X_OK) == 0; }
	const char* env_path = getenv("PATH");
	if (!env_path) { return 0; }
	char copy[NB_EDITOR_FS_PATH_MAX];
	snprintf(copy, sizeof(copy), "%.*s", (int)sizeof(copy) - 1, env_path);
	char* save = NULL;
	for (char* dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		char candidate[NB_EDITOR_FS_PATH_MAX];
		int written = snprintf(candidate, sizeof(candidate), "%s/%s", dir[0] ? dir : ".", name);
		if (written < 0 || (size_t)written >= sizeof(candidate)) { continue; }
		if (access(candidate, X_OK) == 0) { return 1; }
	}
	return 0;
}

const char* editor_fs_find_text_editor(void) {
	static const char* const editors[] = {
		"code-oss", "code", "codium", "vscodium", "subl", "sublime_text", "sublime",
		"gedit", "kate", "mousepad", "pluma", "xed", "leafpad", "atom", "neovide", "nvim-qt",
		"emacs", "nvim", "vim", "vi", "nano", "micro"
	};
	for (size_t i = 0; i < sizeof(editors) / sizeof(editors[0]); i++) {
		if (editor_fs_executable_exists(editors[i])) { return editors[i]; }
	}
	return NULL;
}

int editor_fs_open_external_text(const char* path) {
	if (!editor_fs_is_external_text(path)) { return 0; }
	const char* editor = editor_fs_find_text_editor();
	if (!editor) { return 0; }

	pid_t pid = fork();
	if (pid < 0) { return 0; }
	if (pid == 0) {
		setsid();
		pid_t child = fork();
		if (child < 0) { _exit(127); }
		if (child > 0) { _exit(0); }
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			if (devnull > STDERR_FILENO) { close(devnull); }
		}
		execlp(editor, editor, path, (char*)NULL);
		_exit(127);
	}

	int status = 0;
	waitpid(pid, &status, 0);
	return 1;
}

static int editor_fs_is_boundary(char c) {
	return !isalnum((unsigned char)c) && c != '_' && c != '.';
}

static int editor_fs_replace_refs(const char* src, const char* old_path, const char* new_path, char** out) {
	size_t src_len = strlen(src);
	size_t old_len = strlen(old_path);
	size_t new_len = strlen(new_path);
	if (old_len == 0 || src_len < old_len) {
		*out = (char*)malloc(src_len + 1);
		if (!*out) { return 0; }
		memcpy(*out, src, src_len + 1);
		return 0;
	}

	size_t cap = src_len + 1;
	char* result = (char*)malloc(cap);
	if (!result) { return 0; }
	size_t used = 0;
	int changed = 0;
	for (size_t i = 0; i < src_len;) {
		int matches = i + old_len <= src_len && memcmp(src + i, old_path, old_len) == 0;
		if (matches && editor_fs_is_boundary(i == 0 ? '\0' : src[i - 1]) && editor_fs_is_boundary(src[i + old_len])) {
			if (used + new_len + 1 > cap) {
				size_t next = (used + new_len + 1) * 2;
				char* grown = (char*)realloc(result, next);
				if (!grown) {
					free(result);
					return 0;
				}
				result = grown;
				cap = next;
			}
			memcpy(result + used, new_path, new_len);
			used += new_len;
			i += old_len;
			changed = 1;
			continue;
		}
		if (used + 2 > cap) {
			size_t next = cap * 2;
			char* grown = (char*)realloc(result, next);
			if (!grown) {
				free(result);
				return 0;
			}
			result = grown;
			cap = next;
		}
		result[used++] = src[i++];
	}
	result[used] = '\0';
	*out = result;
	return changed;
}

static int editor_fs_is_text_file(const char* path) {
	const char* ext = editor_fs_ext(path);
	const char* text_exts[] = { "tscn", "tres", "ini", "cfg", "wren", "txt", "md" };
	for (size_t i = 0; i < sizeof(text_exts) / sizeof(text_exts[0]); i++) {
		if (strcmp(ext, text_exts[i]) == 0) { return 1; }
	}
	return 0;
}

static int editor_fs_rewrite_dir(const char* dir, const char* old_path, const char* new_path) {
	DIR* handle = opendir(dir);
	if (!handle) { return 0; }
	int changed = 0;
	struct dirent* item = NULL;
	while ((item = readdir(handle)) != NULL) {
		if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) { continue; }
		if (item->d_name[0] == '.') { continue; }
		char path[NB_EDITOR_FS_PATH_MAX];
		editor_fs_join(path, sizeof(path), dir, item->d_name);
		if (editor_fs_is_dir(path)) {
			if (strcmp(item->d_name, "build") != 0 && strcmp(item->d_name, "third_party") != 0) {
				changed |= editor_fs_rewrite_dir(path, old_path, new_path);
			}
			continue;
		}
		if (!editor_fs_is_text_file(path)) { continue; }
		char* text = NULL;
		if (!editor_fs_read_text(path, &text, NULL)) { continue; }
		char* replaced = NULL;
		int file_changed = editor_fs_replace_refs(text, old_path, new_path, &replaced);
		free(text);
		if (file_changed && replaced) {
			changed |= editor_fs_write_text(path, replaced, strlen(replaced));
		}
		free(replaced);
	}
	closedir(handle);
	return changed;
}

int editor_fs_rewrite_refs(const char* old_path, const char* new_path) {
	if (!old_path || !new_path || strcmp(old_path, new_path) == 0) { return 0; }
	return editor_fs_rewrite_dir(".", old_path, new_path);
}

const char* editor_fs_ext(const char* path) {
	if (!path) { return ""; }
	const char* slash = strrchr(path, '/');
	const char* dot = strrchr(path, '.');
	if (!dot || (slash && dot < slash)) { return ""; }
	return dot + 1;
}

void editor_fs_basename(const char* path, char* out, size_t size) {
	if (!path || !out || size == 0) { return; }
	const char* slash = strrchr(path, '/');
	const char* base = slash ? slash + 1 : path;
	size_t len = strlen(base);
	if (len >= size) { len = size - 1; }
	memcpy(out, base, len);
	out[len] = '\0';
}

void editor_fs_dirname(const char* path, char* out, size_t size) {
	if (!path || !out || size == 0) { return; }
	const char* slash = strrchr(path, '/');
	if (!slash) {
		snprintf(out, size, ".");
		return;
	}
	if (slash == path) {
		snprintf(out, size, "/");
		return;
	}
	size_t len = (size_t)(slash - path);
	if (len >= size) { len = size - 1; }
	memcpy(out, path, len);
	out[len] = '\0';
}
