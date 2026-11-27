class KulaUtil {
	static clamp(v, lo, hi) { v < lo ? lo : (v > hi ? hi : v) }
	static lerp(a, b, t) { a + (b - a) * t }
}
