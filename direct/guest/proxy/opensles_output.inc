#include <aaudio/AAudio.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct OutputObject OutputObject;
typedef struct {
  const void *table;
  OutputObject *owner;
} OutputInterface;
typedef struct {
  const uint8_t *data;
  uint32_t bytes, used;
} OutputBuffer;
struct OutputObject {
  OutputInterface object, engine, play, queue, volume, config;
  SLuint32 kind, state, playState, queueCapacity, head, count, completed;
  SLDataFormat_PCM pcm;
  SLuint32 representation, streamType, performance;
  SLmillibel level;
  SLboolean mute;
  slAndroidSimpleBufferQueueCallback callback;
  void *callbackContext;
  uint64_t frames;
  pthread_mutex_t lock;
  AAudioStream *stream;
  OutputBuffer *buffers;
};
static OutputObject *owner(const void *self) {
  return ((const OutputInterface *)self)->owner;
}
static int iid_equal(SLInterfaceID a, SLInterfaceID b) {
  return a && b && !memcmp(a, b, sizeof(*a));
}
static SLresult unsupported(void) { return SL_RESULT_FEATURE_UNSUPPORTED; }
static SLresult object_interface(SLObjectItf self, const SLInterfaceID iid,
                                 void *result) {
  if (!result)
    return SL_RESULT_PARAMETER_INVALID;
  OutputObject *o = owner(self);
  void *itf = 0;
  if (iid_equal(iid, SL_IID_OBJECT))
    itf = &o->object;
  if (o->kind == SL_OBJECTID_ENGINE && iid_equal(iid, SL_IID_ENGINE))
    itf = &o->engine;
  if (o->kind == SL_OBJECTID_AUDIOPLAYER) {
    if (iid_equal(iid, SL_IID_PLAY))
      itf = &o->play;
    if (iid_equal(iid, SL_IID_BUFFERQUEUE) ||
        iid_equal(iid, SL_IID_ANDROIDSIMPLEBUFFERQUEUE))
      itf = &o->queue;
    if (iid_equal(iid, SL_IID_VOLUME))
      itf = &o->volume;
    if (iid_equal(iid, SL_IID_ANDROIDCONFIGURATION))
      itf = &o->config;
  }
  *(void **)result = itf;
  return itf ? SL_RESULT_SUCCESS : SL_RESULT_FEATURE_UNSUPPORTED;
}
static aaudio_data_callback_result_t output_callback(AAudioStream *stream,
                                                     void *context,
                                                     void *output,
                                                     int32_t frames) {
  (void)stream;
  OutputObject *o = context;
  float *dst = output;
  int32_t done = 0;
  while (done < frames) {
    pthread_mutex_lock(&o->lock);
    if (!o->count || o->playState != SL_PLAYSTATE_PLAYING) {
      pthread_mutex_unlock(&o->lock);
      break;
    }
    OutputBuffer *b = &o->buffers[o->head];
    uint32_t sampleBytes = o->pcm.containerSize / 8,
             frameBytes = sampleBytes * o->pcm.numChannels;
    uint32_t count = (b->bytes - b->used) / frameBytes;
    if (count > (uint32_t)(frames - done))
      count = frames - done;
    float gain = o->mute ? 0.f : powf(10.f, o->level / 2000.f);
    const uint8_t *src = b->data + b->used;
    for (uint32_t i = 0; i < count * o->pcm.numChannels; ++i) {
      float value = 0;
      if (o->representation == SL_ANDROID_PCM_REPRESENTATION_FLOAT &&
          sampleBytes == 4)
        memcpy(&value, src + i * 4, 4);
      else if (sampleBytes == 2) {
        int16_t n;
        memcpy(&n, src + i * 2, 2);
        value = n / 32768.f;
      } else if (sampleBytes == 1)
        value = (src[i] - 128) / 128.f;
      else if (sampleBytes == 4) {
        int32_t n;
        memcpy(&n, src + i * 4, 4);
        value = n / 2147483648.f;
      } else if (sampleBytes == 3) {
        const uint8_t *s = src + i * 3;
        int32_t n = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 |
                              (uint32_t)s[2] << 24);
        value = n / 2147483648.f;
      }
      dst[(size_t)done * o->pcm.numChannels + i] = value * gain;
    }
    b->used += count * frameBytes;
    done += count;
    o->frames += count;
    int completed = b->used == b->bytes;
    slAndroidSimpleBufferQueueCallback callback = o->callback;
    void *user = o->callbackContext;
    if (completed) {
      o->head = (o->head + 1) % o->queueCapacity;
      --o->count;
      ++o->completed;
    }
    pthread_mutex_unlock(&o->lock);
    if (completed && callback)
      callback((SLAndroidSimpleBufferQueueItf)&o->queue, user);
  }
  memset(dst + (size_t)done * o->pcm.numChannels, 0,
         (size_t)(frames - done) * o->pcm.numChannels * sizeof(float));
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static SLresult object_realize(SLObjectItf self, SLboolean async) {
  OutputObject *o = owner(self);
  if (async)
    return SL_RESULT_FEATURE_UNSUPPORTED;
  if (o->state == SL_OBJECT_STATE_REALIZED)
    return SL_RESULT_PRECONDITIONS_VIOLATED;
  if (o->kind == SL_OBJECTID_AUDIOPLAYER) {
    AAudioStreamBuilder *builder = 0;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
      return SL_RESULT_RESOURCE_ERROR;
    AAudioStreamBuilder_setSampleRate(builder, o->pcm.samplesPerSec / 1000);
    AAudioStreamBuilder_setChannelCount(builder, o->pcm.numChannels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setDataCallback(builder, output_callback, o);
    AAudioStreamBuilder_setPerformanceMode(builder,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    aaudio_result_t result =
        AAudioStreamBuilder_openStream(builder, &o->stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK)
      return SL_RESULT_RESOURCE_ERROR;
  }
  o->state = SL_OBJECT_STATE_REALIZED;
  return SL_RESULT_SUCCESS;
}
static SLresult object_state(SLObjectItf self, SLuint32 *state) {
  if (!state)
    return SL_RESULT_PARAMETER_INVALID;
  *state = owner(self)->state;
  return 0;
}
static void object_destroy(SLObjectItf self) {
  OutputObject *o = owner(self);
  if (o->stream) {
    AAudioStream_requestStop(o->stream);
    AAudioStream_close(o->stream);
  }
  pthread_mutex_destroy(&o->lock);
  free(o->buffers);
  free(o);
}
static SLresult play_set(SLPlayItf self, SLuint32 state) {
  OutputObject *o = owner(self);
  if (state < SL_PLAYSTATE_STOPPED || state > SL_PLAYSTATE_PLAYING ||
      !o->stream)
    return SL_RESULT_PRECONDITIONS_VIOLATED;
  pthread_mutex_lock(&o->lock);
  o->playState = state;
  pthread_mutex_unlock(&o->lock);
  aaudio_result_t result =
      state == SL_PLAYSTATE_PLAYING  ? AAudioStream_requestStart(o->stream)
      : state == SL_PLAYSTATE_PAUSED ? AAudioStream_requestPause(o->stream)
                                     : AAudioStream_requestStop(o->stream);
  return result == AAUDIO_OK ? 0 : SL_RESULT_RESOURCE_ERROR;
}
static SLresult play_state(SLPlayItf self, SLuint32 *value) {
  if (!value)
    return 2;
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  *value = o->playState;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult play_position(SLPlayItf self, SLmillisecond *value) {
  if (!value)
    return 2;
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  *value = (SLmillisecond)(o->frames * 1000000 / o->pcm.samplesPerSec);
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult play_duration(SLPlayItf self, SLmillisecond *value) {
  (void)self;
  if (!value)
    return 2;
  *value = SL_TIME_UNKNOWN;
  return 0;
}
static SLresult queue_enqueue(SLAndroidSimpleBufferQueueItf self,
                              const void *data, SLuint32 bytes) {
  OutputObject *o = owner(self);
  if (!data || !bytes ||
      bytes % (o->pcm.numChannels * (o->pcm.containerSize / 8)))
    return SL_RESULT_PARAMETER_INVALID;
  pthread_mutex_lock(&o->lock);
  if (o->count == o->queueCapacity) {
    pthread_mutex_unlock(&o->lock);
    return SL_RESULT_BUFFER_INSUFFICIENT;
  }
  o->buffers[(o->head + o->count) % o->queueCapacity] =
      (OutputBuffer){data, bytes, 0};
  ++o->count;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult queue_clear(SLAndroidSimpleBufferQueueItf self) {
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  o->head = o->count = o->completed = 0;
  pthread_mutex_unlock(&o->lock);
  if (o->stream)
    AAudioStream_requestFlush(o->stream);
  return 0;
}
static SLresult queue_state(SLAndroidSimpleBufferQueueItf self,
                            SLAndroidSimpleBufferQueueState *state) {
  if (!state)
    return 2;
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  state->count = o->count;
  state->index = o->completed;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult queue_callback(SLAndroidSimpleBufferQueueItf self,
                               slAndroidSimpleBufferQueueCallback cb,
                               void *user) {
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  o->callback = cb;
  o->callbackContext = user;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult volume_set(SLVolumeItf self, SLmillibel level) {
  if (level > 0)
    return 2;
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  o->level = level;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult volume_get(SLVolumeItf self, SLmillibel *value) {
  if (!value)
    return 2;
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  *value = o->level;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult volume_max(SLVolumeItf self, SLmillibel *value) {
  (void)self;
  if (!value)
    return 2;
  *value = 0;
  return 0;
}
static SLresult mute_set(SLVolumeItf self, SLboolean value) {
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  o->mute = value;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLresult mute_get(SLVolumeItf self, SLboolean *value) {
  if (!value)
    return 2;
  OutputObject *o = owner(self);
  pthread_mutex_lock(&o->lock);
  *value = o->mute;
  pthread_mutex_unlock(&o->lock);
  return 0;
}
static SLuint32 *configuration(OutputObject *o, const SLchar *key) {
  if (!key)
    return 0;
  if (!strcmp((const char *)key, (const char *)SL_ANDROID_KEY_STREAM_TYPE))
    return &o->streamType;
  if (!strcmp((const char *)key, (const char *)SL_ANDROID_KEY_PERFORMANCE_MODE))
    return &o->performance;
  return 0;
}
static SLresult config_set(SLAndroidConfigurationItf self, const SLchar *key,
                           const void *value, SLuint32 size) {
  OutputObject *o = owner(self);
  SLuint32 *slot = configuration(o, key);
  if (!slot)
    return SL_RESULT_FEATURE_UNSUPPORTED;
  if (!value || size != 4)
    return 2;
  if (o->state != SL_OBJECT_STATE_UNREALIZED)
    return 1;
  memcpy(slot, value, 4);
  return 0;
}
static SLresult config_get(SLAndroidConfigurationItf self, const SLchar *key,
                           SLuint32 *size, void *value) {
  SLuint32 *slot = configuration(owner(self), key);
  if (!slot)
    return SL_RESULT_FEATURE_UNSUPPORTED;
  if (!size)
    return 2;
  SLuint32 capacity = *size;
  *size = 4;
  if (!value)
    return 0;
  if (capacity < 4)
    return SL_RESULT_BUFFER_INSUFFICIENT;
  memcpy(value, slot, 4);
  return 0;
}
static struct SLObjectItf_ objectTable;
static struct SLEngineItf_ engineTable;
static struct SLPlayItf_ playTable;
static struct SLAndroidSimpleBufferQueueItf_ queueTable;
static struct SLVolumeItf_ volumeTable;
static struct SLAndroidConfigurationItf_ configTable;
static pthread_once_t tablesOnce = PTHREAD_ONCE_INIT;
static void initialize_tables(void);
static OutputObject *create_object(SLuint32 kind) {
  pthread_once(&tablesOnce, initialize_tables);
  OutputObject *o = calloc(1, sizeof(*o));
  if (!o)
    return 0;
  pthread_mutex_init(&o->lock, 0);
  o->kind = kind;
  o->state = SL_OBJECT_STATE_UNREALIZED;
  o->playState = SL_PLAYSTATE_STOPPED;
  o->streamType = SL_ANDROID_STREAM_MEDIA;
  o->object = (OutputInterface){&objectTable, o};
  o->engine = (OutputInterface){&engineTable, o};
  o->play = (OutputInterface){&playTable, o};
  o->queue = (OutputInterface){&queueTable, o};
  o->volume = (OutputInterface){&volumeTable, o};
  o->config = (OutputInterface){&configTable, o};
  return o;
}
static SLresult check_interfaces(OutputObject *o, SLuint32 count,
                                 const SLInterfaceID *ids,
                                 const SLboolean *required) {
  if (count && (!ids || !required))
    return 2;
  for (SLuint32 i = 0; i < count; ++i) {
    void *p;
    if (required[i] && object_interface((SLObjectItf)&o->object, ids[i], &p))
      return SL_RESULT_FEATURE_UNSUPPORTED;
  }
  return 0;
}
static SLresult engine_mix(SLEngineItf self, SLObjectItf *out, SLuint32 count,
                           const SLInterfaceID *ids,
                           const SLboolean *required) {
  (void)self;
  if (!out)
    return 2;
  *out = 0;
  OutputObject *o = create_object(SL_OBJECTID_OUTPUTMIX);
  if (!o)
    return 3;
  SLresult result = check_interfaces(o, count, ids, required);
  if (result) {
    object_destroy((SLObjectItf)&o->object);
    return result;
  }
  *out = (SLObjectItf)&o->object;
  return 0;
}
static SLresult engine_player(SLEngineItf self, SLObjectItf *out,
                              SLDataSource *source, SLDataSink *sink,
                              SLuint32 count, const SLInterfaceID *ids,
                              const SLboolean *required) {
  (void)self;
  if (!out || !source || !source->pLocator || !source->pFormat || !sink ||
      !sink->pLocator)
    return 2;
  *out = 0;
  SLDataLocator_AndroidSimpleBufferQueue *locator = source->pLocator;
  SLDataFormat_PCM *pcm = source->pFormat;
  if ((locator->locatorType != SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE &&
       locator->locatorType != SL_DATALOCATOR_BUFFERQUEUE) ||
      (pcm->formatType != SL_DATAFORMAT_PCM &&
       pcm->formatType != SL_ANDROID_DATAFORMAT_PCM_EX) ||
      *(SLuint32 *)sink->pLocator != SL_DATALOCATOR_OUTPUTMIX)
    return SL_RESULT_CONTENT_UNSUPPORTED;
  if (!locator->numBuffers || locator->numBuffers > 1024 || !pcm->numChannels ||
      pcm->numChannels > 8 || !pcm->samplesPerSec ||
      pcm->endianness != SL_BYTEORDER_LITTLEENDIAN ||
      (pcm->containerSize != 8 && pcm->containerSize != 16 &&
       pcm->containerSize != 24 && pcm->containerSize != 32))
    return SL_RESULT_PARAMETER_INVALID;
  OutputObject *o = create_object(SL_OBJECTID_AUDIOPLAYER);
  if (!o)
    return 3;
  o->pcm = *pcm;
  o->queueCapacity = locator->numBuffers;
  o->representation = pcm->formatType == SL_ANDROID_DATAFORMAT_PCM_EX
                          ? ((SLAndroidDataFormat_PCM_EX *)pcm)->representation
                          : SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
  o->buffers = calloc(o->queueCapacity, sizeof(OutputBuffer));
  if (!o->buffers) {
    object_destroy((SLObjectItf)&o->object);
    return 3;
  }
  SLresult result = check_interfaces(o, count, ids, required);
  if (result) {
    object_destroy((SLObjectItf)&o->object);
    return result;
  }
  *out = (SLObjectItf)&o->object;
  return 0;
}
static void fill_table(void *table, size_t bytes) {
  void **slots = table;
  for (size_t i = 0; i < bytes / sizeof(void *); ++i)
    slots[i] = (void *)unsupported;
}
static void initialize_tables(void) {
  fill_table(&objectTable, sizeof(objectTable));
  fill_table(&engineTable, sizeof(engineTable));
  fill_table(&playTable, sizeof(playTable));
  fill_table(&queueTable, sizeof(queueTable));
  fill_table(&volumeTable, sizeof(volumeTable));
  fill_table(&configTable, sizeof(configTable));
  objectTable.Realize = object_realize;
  objectTable.GetState = object_state;
  objectTable.GetInterface = object_interface;
  objectTable.Destroy = object_destroy;
  engineTable.CreateAudioPlayer = engine_player;
  engineTable.CreateOutputMix = engine_mix;
  playTable.SetPlayState = play_set;
  playTable.GetPlayState = play_state;
  playTable.GetDuration = play_duration;
  playTable.GetPosition = play_position;
  queueTable.Enqueue = queue_enqueue;
  queueTable.Clear = queue_clear;
  queueTable.GetState = queue_state;
  queueTable.RegisterCallback = queue_callback;
  volumeTable.SetVolumeLevel = volume_set;
  volumeTable.GetVolumeLevel = volume_get;
  volumeTable.GetMaxVolumeLevel = volume_max;
  volumeTable.SetMute = mute_set;
  volumeTable.GetMute = mute_get;
  configTable.SetConfiguration = config_set;
  configTable.GetConfiguration = config_get;
}
SLresult slCreateEngine(SLObjectItf *out, SLuint32 optionCount,
                        const SLEngineOption *options, SLuint32 count,
                        const SLInterfaceID *ids, const SLboolean *required) {
  (void)optionCount;
  (void)options;
  if (!out)
    return 2;
  *out = 0;
  OutputObject *o = create_object(SL_OBJECTID_ENGINE);
  if (!o)
    return 3;
  SLresult result = check_interfaces(o, count, ids, required);
  if (result) {
    object_destroy((SLObjectItf)&o->object);
    return result;
  }
  *out = (SLObjectItf)&o->object;
  return 0;
}
SLresult slQueryNumSupportedEngineInterfaces(SLuint32 *count) {
  if (!count)
    return 2;
  *count = 1;
  return 0;
}
SLresult slQuerySupportedEngineInterfaces(SLuint32 index, SLInterfaceID *iid) {
  if (!iid || index)
    return 2;
  *iid = SL_IID_ENGINE;
  return 0;
}
