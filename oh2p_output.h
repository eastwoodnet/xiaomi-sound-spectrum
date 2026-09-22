/* OH2P output adapter. The shared renderer still produces its 18 virtual pixels.
 * Both drivers accept 0xBBGGRR; only paths, pixel count and spatial mapping differ.
 */
#define OH2P_LED_COUNT 12
static int oh2p_brightness = 20;

static uint32_t oh2p_pixel(const uint32_t *colors, int pixel) {
    /* Area resampling 18 -> 12: every virtual pixel contributes, rather than
     * dropping frequency bands or the original white peak indicators.
     * Coordinates use 12 units per virtual pixel, 18 per physical pixel.
     * Physical indices run right -> left. Cut the virtual ring through the
     * center of pixel 9: its halves reach both outer ends, while pixel 0
     * is shared equally by physical pixels 5/6. The transform is fixed for
     * every mode; it does not reinterpret frequency bands or animation type.
     * Ring positions 10..17 reach the right half, 1..8 the left half.
     */
    unsigned r = 0, g = 0, b = 0;
    int start = 9 * 12 + 6 + pixel * 18, end = start + 18;
    for (int v = start / 12; v <= (end - 1) / 12; v++) {
        int left = start > v * 12 ? start : v * 12;
        int right = end < (v + 1) * 12 ? end : (v + 1) * 12;
        unsigned weight = (unsigned)(right - left);
        uint32_t color = colors[v % 18];
        r += (color & 255) * weight;
        g += ((color >> 8) & 255) * weight;
        b += ((color >> 16) & 255) * weight;
    }
    r = r * (unsigned)oh2p_brightness / 1800;
    g = g * (unsigned)oh2p_brightness / 1800;
    b = b * (unsigned)oh2p_brightness / 1800;
    return r | (g << 8) | (b << 16);
}
