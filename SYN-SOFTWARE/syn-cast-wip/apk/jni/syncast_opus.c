/* ------------------------------------------------------------------------
 *   libopus for the Java core: the encoder with the Zello app's settings
 *   (docs/REV-zello-7.19.2.md: VOIP, VBR, in-band FEC, 20% expected loss,
 *   complexity 10, bandwidth and bitrate on auto) and a decoder that can
 *   recover a lost packet from the next one's FEC.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <jni.h>
#include <opus.h>
#include <stdint.h>

#define FN(name) Java_com_syntax990_syncast_core_Opus_##name

JNIEXPORT jlong JNICALL FN(encCreate)(JNIEnv *env, jclass cls, jint rate) {
	(void)env;
	(void)cls;
	int err = 0;
	OpusEncoder *e = opus_encoder_create(rate, 1, OPUS_APPLICATION_VOIP, &err);
	if (!e) {
		return 0;
	}
	opus_encoder_ctl(e, OPUS_SET_BANDWIDTH(OPUS_AUTO));
	opus_encoder_ctl(e, OPUS_SET_VBR(1));
	opus_encoder_ctl(e, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(e, OPUS_SET_PACKET_LOSS_PERC(20));
	opus_encoder_ctl(e, OPUS_SET_COMPLEXITY(10));
	opus_encoder_ctl(e, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	return (jlong)(intptr_t)e;
}

JNIEXPORT void JNICALL FN(encReset)(JNIEnv *env, jclass cls, jlong h) {
	(void)env;
	(void)cls;
	opus_encoder_ctl((OpusEncoder *)(intptr_t)h, OPUS_RESET_STATE);
}

JNIEXPORT jint JNICALL FN(encode)(JNIEnv *env, jclass cls, jlong h, jshortArray pcm, jint samples, jbyteArray out) {
	(void)cls;
	jshort *in = (*env)->GetShortArrayElements(env, pcm, NULL);
	jbyte *o = (*env)->GetByteArrayElements(env, out, NULL);
	int n = opus_encode((OpusEncoder *)(intptr_t)h, in, samples, (unsigned char *)o, (*env)->GetArrayLength(env, out));
	(*env)->ReleaseShortArrayElements(env, pcm, in, JNI_ABORT);
	(*env)->ReleaseByteArrayElements(env, out, o, 0);
	return n;
}

JNIEXPORT void JNICALL FN(encDestroy)(JNIEnv *env, jclass cls, jlong h) {
	(void)env;
	(void)cls;
	if (h) {
		opus_encoder_destroy((OpusEncoder *)(intptr_t)h);
	}
}

JNIEXPORT jlong JNICALL FN(decCreate)(JNIEnv *env, jclass cls, jint rate) {
	(void)env;
	(void)cls;
	int err = 0;
	return (jlong)(intptr_t)opus_decoder_create(rate, 1, &err);
}

/* data == NULL: conceal one packet of max_samples */
JNIEXPORT jint JNICALL FN(decode)(JNIEnv *env, jclass cls, jlong h, jbyteArray data, jint len, jshortArray out,
	jint max_samples, jboolean fec) {
	(void)cls;
	jbyte *d = data ? (*env)->GetByteArrayElements(env, data, NULL) : NULL;
	jshort *o = (*env)->GetShortArrayElements(env, out, NULL);
	jint cap = (*env)->GetArrayLength(env, out);
	if (max_samples > cap) {
		max_samples = cap;
	}
	int n = opus_decode((OpusDecoder *)(intptr_t)h, (const unsigned char *)d, d ? len : 0, o, max_samples, fec ? 1 : 0);
	if (d) {
		(*env)->ReleaseByteArrayElements(env, data, d, JNI_ABORT);
	}
	(*env)->ReleaseShortArrayElements(env, out, o, 0);
	return n;
}

JNIEXPORT jint JNICALL FN(packetSamples)(JNIEnv *env, jclass cls, jbyteArray data, jint len, jint rate) {
	(void)cls;
	jbyte *d = (*env)->GetByteArrayElements(env, data, NULL);
	int n = opus_packet_get_nb_samples((const unsigned char *)d, len, rate);
	(*env)->ReleaseByteArrayElements(env, data, d, JNI_ABORT);
	return n;
}

JNIEXPORT void JNICALL FN(decDestroy)(JNIEnv *env, jclass cls, jlong h) {
	(void)env;
	(void)cls;
	if (h) {
		opus_decoder_destroy((OpusDecoder *)(intptr_t)h);
	}
}
