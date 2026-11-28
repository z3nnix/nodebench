from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]

icon_files = [
    ("Node3D.svg", "nb_icon_node3d_svg"),
    ("Camera3D.svg", "nb_icon_camera3d_svg"),
    ("MeshInstance3D.svg", "nb_icon_mesh_instance3d_svg"),
    ("DirectionalLight3D.svg", "nb_icon_directional_light3d_svg"),
    ("CollisionShape3D.svg", "nb_icon_collision_shape3d_svg"),
    ("Script.svg", "nb_icon_script_svg"),
    ("AudioStreamPlayer3D.svg", "nb_icon_audio_stream_player3d_svg"),
    ("PackedScene.svg", "nb_icon_packed_scene_svg"),
    ("Folder.svg", "nb_icon_folder_svg"),
    ("File.svg", "nb_icon_file_svg"),
    ("TextFile.svg", "nb_icon_text_file_svg"),
    ("Object.svg", "nb_icon_object_svg"),
    ("AudioStream.svg", "nb_icon_audio_stream_svg"),
    ("Mesh.svg", "nb_icon_mesh_svg"),
    ("Texture2D.svg", "nb_icon_texture2d_svg"),
    ("Plus.svg", "nb_icon_plus_svg"),
    ("Import.svg", "nb_icon_import_svg"),
    ("Search.svg", "nb_icon_search_svg"),
    ("Settings.svg", "nb_icon_settings_svg"),
    ("Play.svg", "nb_icon_play_svg"),
    ("Edit.svg", "nb_icon_edit_svg"),
    ("Trash.svg", "nb_icon_trash_svg"),
    ("Tag.svg", "nb_icon_tag_svg"),
    ("ChevronDown.svg", "nb_icon_chevron_down_svg"),
]

fonts = [
    ("nb_font_ibm_plex_mono_regular_ttf", "assets/fonts/IBMPlexMono-Regular.ttf"),
    ("nb_font_ibm_plex_mono_semibold_ttf", "assets/fonts/IBMPlexMono-SemiBold.ttf"),
]


def escape_char(ch):
    if ch == "\\":
        return "\\\\"
    if ch == '"':
        return '\\"'
    if ch == "\n":
        return "\\n"
    if ch == "\r":
        return "\\r"
    if ch == "\t":
        return "\\t"
    code = ord(ch)
    if code < 32 or code > 126:
        return "".join("\\{:03o}".format(byte) for byte in ch.encode("utf-8"))
    return ch


def emit_binary(name, data):
    lines = ["static unsigned char {}[] = {{".format(name)]
    for index in range(0, len(data), 16):
        chunk = data[index:index + 16]
        values = ", ".join("0x{:02x}".format(byte) for byte in chunk)
        lines.append("\t{},".format(values))
    lines.append("};")
    return lines


def emit_text(name, data):
    text = data.decode("utf-8")
    parts = text.splitlines(True)
    if not parts:
        parts = [""]
    lines = ["static char {}[] =".format(name)]
    for index, part in enumerate(parts):
        tail = "," if index + 1 < len(parts) else ";"
        lines.append('\t"{}"{}'.format("".join(escape_char(ch) for ch in part), tail))
    return lines


def main():
    if len(sys.argv) != 2:
        raise SystemExit(1)

    lines = ["#ifndef NB_EDITOR_ASSETS_H", "#define NB_EDITOR_ASSETS_H", "", "#ifdef NB_EDITOR_ASSETS_INCLUDE_FONTS", ""]
    for name, relative in fonts:
        lines.extend(emit_binary(name, (root / relative).read_bytes()))
        lines.append("")
    lines.extend(["#endif", ""])
    icon_names = []
    for file_name, name in icon_files:
        lines.extend(emit_text(name, (root / "assets/icons" / file_name).read_bytes()))
        icon_names.append(name)
        lines.append("")
    lines.append("#define NB_EDITOR_ICON_ASSET_COUNT {}".format(len(icon_names)))
    lines.append("static char* const nb_editor_icon_data[NB_EDITOR_ICON_ASSET_COUNT] = {")
    lines.append("\t" + ", ".join(icon_names) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif")

    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
