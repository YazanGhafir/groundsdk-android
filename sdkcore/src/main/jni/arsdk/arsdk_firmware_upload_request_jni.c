/*
 *	 Copyright (C) 2019 Parrot Drones SAS
 *
 *	 Redistribution and use in source and binary forms, with or without
 *	 modification, are permitted provided that the following conditions
 *	 are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in
 *	   the documentation and/or other materials provided with the
 *	   distribution.
 *	 * Neither the name of the Parrot Company nor the names
 *	   of its contributors may be used to endorse or promote products
 *	   derived from this software without specific prior written
 *	   permission.
 *
 *	 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *	 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *	 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *	 FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *	 PARROT COMPANY BE LIABLE FOR ANY DIRECT, INDIRECT,
 *	 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *	 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *	 OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *	 AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *	 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 *	 OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *	 SUCH DAMAGE.
 *
 */

#include "arsdkcore.h"

#define ARSDK_LOG_TAG ftp
#include "arsdk_log.h"

#include <sdkcore/sdkcore_jni.h>

/** Static JNI method id cache */
static struct
{
	/** on ArsdkFirmwareUploadRequest */
	jmethodID jmid_request_progress; /** onRequestProgress */
	jmethodID jmid_request_status;   /** onRequestStatus */
} s_jni_cache;

static void request_progress(struct arsdk_updater_itf *itf,
		struct arsdk_updater_req_upload *request, float percent,
		void *userdata)
{
	JNIEnv *env = NULL;
	int res = (*sdkcore_jvm)->GetEnv(sdkcore_jvm, (void **) &env,
			SDKCORE_JNI_VERSION);
	RETURN_IF_FAILED(env != NULL, res);

	(*env)->CallVoidMethod(env, (jobject) userdata,
			s_jni_cache.jmid_request_progress, (jfloat) percent);
}

static void request_complete(struct arsdk_updater_itf *itf,
		struct arsdk_updater_req_upload *request,
		enum arsdk_updater_req_status status, int error, void *userdata)
{
	JNIEnv *env = NULL;
	int res = (*sdkcore_jvm)->GetEnv(sdkcore_jvm, (void **) &env,
			SDKCORE_JNI_VERSION);
	RETURN_IF_FAILED(env != NULL, res);

	(*env)->CallVoidMethod(env, (jobject) userdata,
			s_jni_cache.jmid_request_status, (jint) status);

	(*env)->DeleteGlobalRef(env, (jobject) userdata);
}

/**
 * Creates and runs a firmware upload request.
 * @param[in] env: JNI env pointer
 * @param[in] jself: reference on java ArsdkFirmwareUploadRequest instance
 * @param[in] arsdkNativePtr: pointer to the native arsdkcore instance
 * @param[in] deviceHandle: native handle of the device that will perform
 *            the upload
 * @param[in] deviceType: type of the device whose firmware must be uploaded
 * @param[in] firmwarePath: path of the firmware file to upload
 * @return the pointer to the ArsdkFirmwareUploadRequest native backend instance
 *         if successful, 0 otherwise
 */
JNIEXPORT jlong JNICALL
Java_com_parrot_drone_sdkcore_arsdk_firmware_ArsdkFirmwareUploadRequest_nativeCreate(
		JNIEnv *env, jobject jself, jlong arsdkNativePtr, jshort deviceHandle,
		jint deviceType, jstring firmwarePath)
{
	struct arsdkcore *arsdk = (struct arsdkcore *)(uintptr_t) arsdkNativePtr;
	RETURN_VAL_IF_FAILED(arsdk != NULL, -EINVAL, 0);

	struct arsdk_device *device = arsdkcore_get_device(arsdk,
			(uint16_t) deviceHandle);
	RETURN_VAL_IF_FAILED(device != NULL, -ENODEV, 0);

	struct arsdk_updater_itf *updater_itf = NULL;
	int res = arsdk_device_get_updater_itf(device, &updater_itf);
	RETURN_VAL_IF_FAILED(updater_itf != NULL, res, 0);

	jself = (*env)->NewGlobalRef(env, jself);
	RETURN_VAL_IF_FAILED(jself != NULL, -ENOMEM, 0);

	const char *firmware_path =
			(*env)->GetStringUTFChars(env, firmwarePath, NULL);
	GOTO_IF_FAILED(firmware_path != NULL, -ENOMEM, err);

	struct arsdk_updater_req_upload_cbs cbs = {
		.progress = &request_progress,
		.complete = &request_complete,
		.userdata = jself
	};

	struct arsdk_updater_req_upload *request = NULL;
	res = arsdk_updater_itf_create_req_upload(updater_itf, firmware_path,
				(enum arsdk_device_type) deviceType, &cbs, &request);

	(*env)->ReleaseStringUTFChars(env, firmwarePath, firmware_path);

	GOTO_IF_FAILED(request != NULL, res, err);

	return (jlong) (uintptr_t) request;

err:
	(*env)->DeleteGlobalRef(env, jself);
	return 0;
}

/**
 * Cancels a running firmware upload request.
 * @param[in] env: JNI env pointer
 * @param[in] clazz: class where this static java method is defined
 * @param[in] nativePtr: pointer to the ArsdkFirmwareUploadRequest native
 *            backend instance
 */
JNIEXPORT void JNICALL
Java_com_parrot_drone_sdkcore_arsdk_firmware_ArsdkFirmwareUploadRequest_nativeCancel(
		JNIEnv *env, jclass clazz, jlong nativePtr)
{
	struct arsdk_updater_req_upload *request =
			(struct arsdk_updater_req_upload *) (uintptr_t) nativePtr;
	RETURN_IF_FAILED(request != NULL, -EINVAL);

	LOG_IF_ERR(arsdk_updater_req_upload_cancel(request));
}

/**
 * Initializes the static method id cache. Called once from static java block.
 * @param[in] env: JNI env pointer
 * @param[in] clazz: class where this static java method is defined
 */
JNIEXPORT void JNICALL
Java_com_parrot_drone_sdkcore_arsdk_firmware_ArsdkFirmwareUploadRequest_nativeClassInit(
		JNIEnv *env, jclass clazz)
{
	s_jni_cache.jmid_request_progress = (*env)->GetMethodID(env, clazz,
			"onRequestProgress", "(F)V");
	s_jni_cache.jmid_request_status = (*env)->GetMethodID(env, clazz,
			"onRequestStatus", "(I)V");
}