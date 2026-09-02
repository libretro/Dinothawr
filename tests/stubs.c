/* Dinothawr - stubs for the test programs.
 *
 * The tilemap reaches the surface cache, which reaches the audio
 * managers, whose mixer hooks are defined in libretro.cpp. A test that
 * only reads a map wants none of that, and linking the frontend to get
 * it would drag in the whole core. These three satisfy the linker and
 * say there is no audio.
 */

int   icy_audio_is_float(void) { return 0; }
void *icy_mixer_f32(void)      { return 0; }
void *icy_mixer_i16(void)      { return 0; }
