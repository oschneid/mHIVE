/*===============================================================================================
 GenerateTone Example
 Copyright (c), Firelight Technologies Pty, Ltd 2011.

 This example shows how simply play generated tones using FMOD_System_PlayDSP instead of
 manually connecting and disconnecting DSP units.
===============================================================================================*/

#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <semaphore.h>
#include "fmod.h"
#include "fmod_errors.h"


#define NUM_SOUNDS 3

FMOD_SYSTEM  *gSystem  = 0;
FMOD_CHANNEL *gChannel = 0;
FMOD_DSP	 *gDSP	   = 0;
float gFrequency = 440.0f;

#define SIG SIGALRM

//ADSR envelope timer & data
FMOD_DSP	 *gADSRDSP   = 0;
const int AMPLITUDE_UPDATE_TIMER_INTERVAL_IN_NS = 10000000;
typedef struct
{
	long attack, decay, release;//in ms
	float sustain; //from 0 to 1
	long start_time; //ms
	long release_time; //ms
	jboolean note_on, enabled;
	sem_t sem;
} ADSRSettings;

//TODO: Make this local!
ADSRSettings *gADSRSettings;

//Recording data
typedef struct
{
	float *buffer;
	unsigned long pos;
	jboolean recording;
	sem_t sem;
} RecordingData;
//TODO: Make this local as well
RecordingData *gRecordingData;
const long RECORDING_BUFFER_SIZE_IN_SECONDS=60;
const long RECORDING_BUFFER_SIZE_IN_SAMPLES=0;
FMOD_DSP	 *gRecordingDSP   = 0;


//Waveform data
const long OSCILLATOR_SINE = 0;
const long OSCILLATOR_SQUARE = 1;
const long OSCILLATOR_SAWUP = 2;
//NO IDEA WHAT #3 IS...
const long OSCILLATOR_TRIANGLE = 4;
//NOT SUPPORTING NOISE FOR NOW
//const unsigned long OSCILLATOR_NOISE = 5;



#define CHECK_RESULT(x) \
{ \
	FMOD_RESULT _result = x; \
	if (_result != FMOD_OK) \
	{ \
		__android_log_print(ANDROID_LOG_ERROR, "fmod", "FMOD error! (%d) %s\n%s:%d", _result, FMOD_ErrorString(_result), __FILE__, __LINE__); \
		exit(-1); \
	} \
}


long GetCurrentTimeMillis()
{
    struct timeval curTime;
    gettimeofday(&curTime, NULL);
	long millis = curTime.tv_sec * 1000 + curTime.tv_usec / 1000;
	return millis;
}

void InitializeRecordingData()
{
    if(gRecordingData->buffer != NULL)
    {
    	free(gRecordingData->buffer);
    }
	gRecordingData->buffer=(float*)malloc(sizeof(float)*RECORDING_BUFFER_SIZE_IN_SAMPLES);
    gRecordingData->pos=0;
    gRecordingData->recording=JNI_FALSE;
}


FMOD_RESULT F_CALLBACK DSPRecord(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int outchannels)
{
    unsigned int count;
    int count2;
    FMOD_DSP *thisdsp = dsp_state->instance;

	sem_wait(&(gRecordingData->sem));

    /*
        This loop assumes inchannels = outchannels, which it will be if the DSP is created with '0'
        as the number of channels in FMOD_DSP_DESCRIPTION.
        Specifying an actual channel count will mean you have to take care of any number of channels coming in,
        but outputting the number of channels specified.  Generally it is best to keep the channel
        count at 0 for maximum compatibility.
    */
    for (count = 0; count < length; count++)
    {
        /*
            Feel free to unroll this.
        */
        for (count2 = 0; count2 < outchannels; count2++)
        {
            /*
                This DSP stores the output
            */
            outbuffer[(count * outchannels) + count2] = inbuffer[(count * inchannels) + count2];
            if(gRecordingData->pos <RECORDING_BUFFER_SIZE_IN_SAMPLES)
			{
				gRecordingData->buffer[gRecordingData->pos++] = outbuffer[(count * outchannels) + count2];
			}
        }
    }

	sem_post(&(gRecordingData->sem));

    return FMOD_OK;
}



FMOD_RESULT F_CALLBACK ADSRCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int outchannels)
{
    unsigned int count;
    int count2;
    ADSRSettings *adsrSettings;
    FMOD_DSP *thisdsp = dsp_state->instance;

	sem_wait(&(gADSRSettings->sem));



    //FMOD_DSP_GetUserData(thisdsp, (void **)&adsrSettings);
    adsrSettings = gADSRSettings;
    long millis = GetCurrentTimeMillis();

    long elapsed = millis - adsrSettings->start_time;

    float fraction = 1.0f;
    if (adsrSettings->enabled == JNI_TRUE)
    {
		if(adsrSettings->note_on == JNI_TRUE)
		{
			if (elapsed <= adsrSettings->attack)
			{
				fraction = ((float)elapsed) / ((float)adsrSettings->attack);
			} else if (elapsed <= adsrSettings->attack + adsrSettings->decay)
			{
				fraction = adsrSettings->sustain
						+ (1.0f-adsrSettings->sustain)* (1.0f-((float)(elapsed-adsrSettings->attack)) / ((float)adsrSettings->decay));
			} else
			{
				fraction = adsrSettings->sustain;
			}

		} else
		{
			elapsed = millis - adsrSettings->release_time;
			if(elapsed <= adsrSettings->release)
			{
				fraction = adsrSettings->sustain * (1.0f - ((float)elapsed)/((float)adsrSettings->release));
			} else
			{
				fraction = 0;
			}
		}
    } else
    {
    	if (gADSRSettings->note_on == JNI_FALSE)
    	{
    		fraction = 0;
    	}
    }

    /*
        This loop assumes inchannels = outchannels, which it will be if the DSP is created with '0'
        as the number of channels in FMOD_DSP_DESCRIPTION.
        Specifying an actual channel count will mean you have to take care of any number of channels coming in,
        but outputting the number of channels specified.  Generally it is best to keep the channel
        count at 0 for maximum compatibility.
    */
    for (count = 0; count < length; count++)
    {
        /*
            Feel free to unroll this.
        */
        for (count2 = 0; count2 < outchannels; count2++)
        {
            /*
                This DSP filter just fractions the volume!
                Input is modified, and sent to output.
            */
            outbuffer[(count * outchannels) + count2] = inbuffer[(count * inchannels) + count2] * fraction;
        }
    }

	sem_post(&(gADSRSettings->sem));

    return FMOD_OK;
}

void Java_org_spin_mhive_HIVEAudioGenerator_cBegin(JNIEnv *env, jobject thiz)
{
	FMOD_RESULT result = FMOD_OK;

	result = FMOD_System_Create(&gSystem);
	CHECK_RESULT(result);

	result = FMOD_System_Init(gSystem, 32, FMOD_INIT_NORMAL, 0);
	CHECK_RESULT(result);

    /*
        Create an oscillator DSP unit for the tone.
    */
    result = FMOD_System_CreateDSPByType(gSystem, FMOD_DSP_TYPE_OSCILLATOR, &gDSP);
    CHECK_RESULT(result);
    result = FMOD_DSP_SetParameter(gDSP, FMOD_DSP_OSCILLATOR_RATE, 440.0f);       /* musical note 'A' */
    CHECK_RESULT(result);

    //add in ADSR
    gADSRSettings = (ADSRSettings*)malloc(sizeof(ADSRSettings));
    gADSRSettings->attack = 0;
    gADSRSettings->decay = 0;
    gADSRSettings->sustain = 1.0f;
    gADSRSettings->release = 0;
    gADSRSettings->start_time = GetCurrentTimeMillis();
    gADSRSettings->release_time = GetCurrentTimeMillis();
    gADSRSettings->note_on = JNI_FALSE;
    sem_init(&(gADSRSettings->sem), 0, 1);

	FMOD_DSP_DESCRIPTION  dspdesc;
	memset(&dspdesc, 0, sizeof(FMOD_DSP_DESCRIPTION));
	strcpy(dspdesc.name, "ADSR Envelope");
	dspdesc.channels     = 0;                   // 0 = whatever comes in, else specify.
	dspdesc.read         = ADSRCallback;
	dspdesc.userdata     = NULL;

	result = FMOD_System_CreateDSP(gSystem, &dspdesc, &gADSRDSP);
	CHECK_RESULT(result);
	//		Inactive by default.
	//FMOD_DSP_SetBypass(gADSRDSP, 1);
	//		Add to system
	result = FMOD_System_AddDSP(gSystem, gADSRDSP, 0);
	CHECK_RESULT(result);


//add in recording DSP
    gRecordingData = (RecordingData*)malloc(sizeof(RecordingData));
	gRecordingData->buffer = NULL;
	InitializeRecordingData();
	sem_init(&(gRecordingData->sem), 0, 1);

	FMOD_DSP_DESCRIPTION  recording_dspdesc;
	memset(&recording_dspdesc, 0, sizeof(FMOD_DSP_DESCRIPTION));
	strcpy(recording_dspdesc.name, "Capture Output");
	recording_dspdesc.channels     = 0;                   // 0 = whatever comes in, else specify.
	recording_dspdesc.read         = DSPRecord;
	recording_dspdesc.userdata     = NULL;

	result = FMOD_System_CreateDSP(gSystem, &recording_dspdesc, &gRecordingDSP);
	CHECK_RESULT(result);
//		Inactive by default.
	//FMOD_DSP_SetBypass(gRecordingDSP, 1);
//		Add to system
	result = FMOD_System_AddDSP(gSystem, gRecordingDSP, 0);
	CHECK_RESULT(result);


    /* Play */
	result = FMOD_System_PlayDSP(gSystem, FMOD_CHANNEL_REUSE, gDSP, 1, &gChannel);
	CHECK_RESULT(result);
}

void Java_org_spin_mhive_HIVEAudioGenerator_cUpdate(JNIEnv *env, jobject thiz)
{
	FMOD_RESULT	result = FMOD_OK;

	result = FMOD_System_Update(gSystem);
	CHECK_RESULT(result);
}

void Java_org_spin_mhive_HIVEAudioGenerator_cEnd(JNIEnv *env, jobject thiz)
{
	FMOD_RESULT result = FMOD_OK;

	//TODO: free the RecordingDSP
	result = FMOD_DSP_Release(gADSRDSP);
	CHECK_RESULT(result);
	free(gADSRDSP);
	if(gRecordingData->buffer != NULL)
	{
		free(gRecordingData->buffer);
	}
	sem_destroy(&(gADSRSettings->sem));
	sem_destroy(&(gRecordingData->sem));
	free(gADSRSettings);
	free(gRecordingData);

    result = FMOD_DSP_Release(gDSP);
    CHECK_RESULT(result);

	result = FMOD_System_Release(gSystem);
	CHECK_RESULT(result);

}

void Java_org_spin_mhive_HIVEAudioGenerator_cSetWaveform(JNIEnv *env, jlong jWaveform)
{
	FMOD_RESULT result = FMOD_ERR_UNSUPPORTED;

	long waveform = (long)jWaveform;
	if(		waveform == OSCILLATOR_SINE
			|| waveform == OSCILLATOR_SQUARE
			|| waveform == OSCILLATOR_SAWUP
			|| waveform == OSCILLATOR_TRIANGLE
			)
	{
		result = FMOD_System_PlayDSP(gSystem, FMOD_CHANNEL_REUSE, gDSP, 1, &gChannel);
		CHECK_RESULT(result);
		result = FMOD_DSP_SetParameter(gDSP, FMOD_DSP_OSCILLATOR_TYPE, waveform);
		FMOD_Channel_SetPaused(gChannel, 0);
	}
    CHECK_RESULT(result);
}

jboolean Java_org_spin_mhive_HIVEAudioGenerator_cGetIsChannelPlaying(JNIEnv *env, jobject thiz)
{
	int isplaying = 0;

	FMOD_Channel_IsPlaying(gChannel, &isplaying);

	return isplaying;
}

jfloat Java_org_spin_mhive_HIVEAudioGenerator_cGetChannelFrequency(JNIEnv *env, jobject thiz)
{
	return gFrequency;
}

jfloat Java_org_spin_mhive_HIVEAudioGenerator_cGetChannelVolume(JNIEnv *env, jobject thiz)
{
	float volume = 0.0f;

	FMOD_Channel_GetVolume(gChannel, &volume);

	return volume;
}

jfloat Java_org_spin_mhive_HIVEAudioGenerator_cGetChannelPan(JNIEnv *env, jobject thiz)
{
	float pan = 0.0f;

	FMOD_Channel_GetPan(gChannel, &pan);

	return pan;
}

void Java_org_spin_mhive_HIVEAudioGenerator_cSetChannelVolume(JNIEnv *env, jobject thiz, jfloat volume)
{
	FMOD_Channel_SetVolume(gChannel, volume);
}

void Java_org_spin_mhive_HIVEAudioGenerator_cSetChannelFrequency(JNIEnv *env, jobject thiz, jfloat frequency)
{
	gFrequency = (float)frequency;
	FMOD_RESULT result = FMOD_OK;
	result = FMOD_DSP_SetParameter(gDSP, FMOD_DSP_OSCILLATOR_RATE, gFrequency);
	CHECK_RESULT(result);
}

void Java_org_spin_mhive_HIVEAudioGenerator_cSetChannelPan(JNIEnv *env, jobject thiz, jfloat pan)
{
	FMOD_Channel_SetPan(gChannel, pan);
}


//ADSR Functions
void Java_org_spin_mhive_HIVEAudioGenerator_cNoteOn(JNIEnv *env, jobject thiz)
{
	sem_wait(&(gADSRSettings->sem));
	gADSRSettings->start_time = GetCurrentTimeMillis();
//	newADSRSettings->release_time = 0;
	gADSRSettings->note_on = JNI_TRUE;
	sem_post(&(gADSRSettings->sem));
}

void Java_org_spin_mhive_HIVEAudioGenerator_cNoteOff(JNIEnv *env, jobject thiz)
{
	sem_wait(&(gADSRSettings->sem));
	gADSRSettings->release_time = GetCurrentTimeMillis();
	gADSRSettings->note_on = JNI_FALSE;
	sem_post(&(gADSRSettings->sem));
}

void Java_org_spin_mhive_HIVEAudioGenerator_cSetADSR(JNIEnv *env, jobject thiz, jint attack, jint decay, jfloat sustain, jint release)
{
	sem_wait(&(gADSRSettings->sem));
	gADSRSettings->attack = attack;
	gADSRSettings->decay = decay;
	gADSRSettings->sustain = (float)sustain;
	gADSRSettings->release = release;
	sem_post(&(gADSRSettings->sem));
}


void Java_org_spin_mhive_HIVEAudioGenerator_cSetADSREnabled(JNIEnv *env, jobject thiz, jboolean b)
{
	sem_wait(&(gADSRSettings->sem));
	gADSRSettings->enabled = b;
	sem_post(&(gADSRSettings->sem));
}

jboolean Java_org_spin_mhive_HIVEAudioGenerator_cGetADSREnabled(JNIEnv *env, jobject thiz)
{
	return gADSRSettings->enabled;
}

jlong Java_org_spin_mhive_HIVEAudioGenerator_cGetADSRAttack(JNIEnv *env, jobject thiz)
{
	if(gADSRSettings != NULL)
	{
		return (jlong)gADSRSettings->attack;
	}
	return 0;
}

jlong Java_org_spin_mhive_HIVEAudioGenerator_cGetADSRDecay(JNIEnv *env, jobject thiz)
{
	if(gADSRSettings != NULL)
	{
		return (jlong)gADSRSettings->decay;
	}
	return 0;
}

jfloat Java_org_spin_mhive_HIVEAudioGenerator_cGetADSRSustain(JNIEnv *env, jobject thiz)
{
	if(gADSRSettings != NULL)
	{
		return (jfloat)gADSRSettings->sustain;
	}
	return 0.0f;
}

jlong Java_org_spin_mhive_HIVEAudioGenerator_cGetADSRRelease(JNIEnv *env, jobject thiz)
{
	if(gADSRSettings != NULL)
	{
		return (jlong)gADSRSettings->release;
	}
	return 0;
}



jboolean Java_org_spin_mhive_HIVEAudioGenerator_cStartRecording(JNIEnv *env, jobject thiz)
{
	sem_wait(&(gRecordingData->sem));
	InitializeRecordingData();
	gRecordingData->recording = JNI_TRUE;

	sem_post(&(gRecordingData->sem));
	return JNI_FALSE;
}

jboolean Java_org_spin_mhive_HIVEAudioGenerator_cStopRecording(JNIEnv *env, jobject thiz)
{
	sem_wait(&(gRecordingData->sem));

	gRecordingData->recording = JNI_FALSE;
	free(gRecordingData->buffer);
	gRecordingData->buffer = NULL;

	sem_post(&(gRecordingData->sem));
	return JNI_FALSE;
}

