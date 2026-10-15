#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#define CHECK(call)                                                            \
  do {                                                                         \
    SLresult r = (call);                                                       \
    if (r) {                                                                   \
      fprintf(stderr, "%s failed: %u\n", #call, r);                            \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static short samples[960];
static atomic_int callbacks;
static atomic_int callbackError;
static void refill(SLAndroidSimpleBufferQueueItf queue, void *context) {
  (void)context;
  if (atomic_fetch_add(&callbacks, 1) < 8 &&
      (*queue)->Enqueue(queue, samples, sizeof samples))
    atomic_store(&callbackError, 1);
}
int main(void) {
  SLObjectItf engineObject = 0, mix = 0, player = 0;
  SLEngineItf engine = 0;
  SLPlayItf play = 0;
  SLAndroidSimpleBufferQueueItf queue = 0;
  CHECK(slCreateEngine(&engineObject, 0, 0, 0, 0, 0));
  CHECK((*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE));
  CHECK((*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engine));
  CHECK((*engine)->CreateOutputMix(engine, &mix, 0, 0, 0));
  CHECK((*mix)->Realize(mix, SL_BOOLEAN_FALSE));
  SLDataLocator_AndroidSimpleBufferQueue locator = {
      SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 3};
  SLDataFormat_PCM pcm = {SL_DATAFORMAT_PCM,
                          2,
                          SL_SAMPLINGRATE_48,
                          16,
                          16,
                          SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
                          SL_BYTEORDER_LITTLEENDIAN};
  SLDataSource source = {&locator, &pcm};
  SLDataLocator_OutputMix output = {SL_DATALOCATOR_OUTPUTMIX, mix};
  SLDataSink sink = {&output, 0};
  SLInterfaceID ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_VOLUME};
  SLboolean required[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
  CHECK((*engine)->CreateAudioPlayer(engine, &player, &source, &sink, 2, ids,
                                     required));
  CHECK((*player)->Realize(player, SL_BOOLEAN_FALSE));
  CHECK((*player)->GetInterface(player, SL_IID_PLAY, &play));
  CHECK(
      (*player)->GetInterface(player, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &queue));
  CHECK((*queue)->RegisterCallback(queue, refill, 0));
  CHECK((*queue)->Enqueue(queue, samples, sizeof samples));
  CHECK((*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING));
  for (int i = 0; i < 300 && atomic_load(&callbacks) < 8; ++i)
    usleep(10000);
  if (atomic_load(&callbacks) < 8 || atomic_load(&callbackError)) {
    fprintf(stderr, "queue callback failed\n");
    return 1;
  }
  CHECK((*play)->SetPlayState(play, SL_PLAYSTATE_PAUSED));
  CHECK((*queue)->Clear(queue));
  SLAndroidSimpleBufferQueueState state;
  CHECK((*queue)->GetState(queue, &state));
  if (state.count)
    return 1;
  CHECK((*play)->SetPlayState(play, SL_PLAYSTATE_STOPPED));
  (*player)->Destroy(player);
  (*mix)->Destroy(mix);
  (*engineObject)->Destroy(engineObject);
  puts("OPENSL_ES_SELFTEST_PASS");
  return 0;
}
