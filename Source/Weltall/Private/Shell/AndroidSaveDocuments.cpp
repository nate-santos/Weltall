#include "Shell/AndroidSaveDocuments.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"

namespace
{
	constexpr jsize MaxSaveBytes = 1024 * 1024;

	/** Uses the current activity on each call, including after surface/activity recreation. */
	struct FSaveActivity
	{
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		jobject Activity = FAndroidApplication::GetGameActivityThis();
		jclass Class = Env && Activity ? Env->GetObjectClass(Activity) : nullptr;
		~FSaveActivity() { if (Class) { Env->DeleteLocalRef(Class); } }
		bool Failed()
		{
			if (!Env) { return true; }
			if (!Env->ExceptionCheck()) { return false; }
			Env->ExceptionClear();
			return true;
		}
		jmethodID Method(const char* Name, const char* Signature)
		{
			if (!Class || Failed()) { return nullptr; }
			jmethodID Result = Env->GetMethodID(Class, Name, Signature);
			return Failed() ? nullptr : Result;
		}
	};

	class FAndroidSaveDocuments final : public Weltall::ISaveDocuments
	{
	public:
		bool Import() override
		{
			FSaveActivity Java;
			const jmethodID Method = Java.Method("AndroidThunkJava_WeltallImportSaveDocument", "()Z");
			if (!Method) { return false; }
			const bool Started = Java.Env->CallBooleanMethod(Java.Activity, Method) == JNI_TRUE;
			return !Java.Failed() && Started;
		}

		bool Export(std::span<const Weltall::uint8> Bytes, const std::string& SuggestedName) override
		{
			if (Bytes.size() > static_cast<std::size_t>(MaxSaveBytes)) { return false; }
			FSaveActivity Java;
			const jmethodID Method = Java.Method("AndroidThunkJava_WeltallExportSaveDocument", "([BLjava/lang/String;)Z");
			if (!Method) { return false; }
			jbyteArray Data = Java.Env->NewByteArray(static_cast<jsize>(Bytes.size()));
			if (Java.Failed() || !Data) { return false; }
			jstring Name = Java.Env->NewStringUTF(SuggestedName.c_str());
			if (Java.Failed() || !Data || !Name)
			{
				if (Data) { Java.Env->DeleteLocalRef(Data); }
				if (Name) { Java.Env->DeleteLocalRef(Name); }
				return false;
			}
			Java.Env->SetByteArrayRegion(Data, 0, static_cast<jsize>(Bytes.size()), reinterpret_cast<const jbyte*>(Bytes.data()));
			const bool Copied = !Java.Failed();
			const bool Started = Copied && Java.Env->CallBooleanMethod(Java.Activity, Method, Data, Name) == JNI_TRUE;
			const bool Failed = Java.Failed();
			Java.Env->DeleteLocalRef(Data);
			Java.Env->DeleteLocalRef(Name);
			return !Failed && Started;
		}

		Weltall::FSaveDocumentResult Poll() override
		{
			using Weltall::ESaveDocumentStatus;
			Weltall::FSaveDocumentResult Result{ESaveDocumentStatus::Failed, {}};
			FSaveActivity Java;
			const jmethodID Method = Java.Method("AndroidThunkJava_WeltallPollSaveDocument", "()[B");
			if (!Method) { return Result; }
			jbyteArray Data = static_cast<jbyteArray>(Java.Env->CallObjectMethod(Java.Activity, Method));
			if (Java.Failed() || !Data) { return Result; }
			const jsize Size = Java.Env->GetArrayLength(Data);
			if (Size > 0 && Size <= MaxSaveBytes + 1)
			{
				jbyte Status = 0;
				Java.Env->GetByteArrayRegion(Data, 0, 1, &Status);
				if (Status > 0 && Status <= static_cast<jbyte>(ESaveDocumentStatus::TooLarge))
				{
					Result.Status = static_cast<ESaveDocumentStatus>(Status);
					if (Result.Status == ESaveDocumentStatus::Imported)
					{
						Result.Bytes.resize(Size - 1);
						Java.Env->GetByteArrayRegion(Data, 1, Size - 1, reinterpret_cast<jbyte*>(Result.Bytes.data()));
					}
				}
			}
			if (Java.Failed()) { Result = {ESaveDocumentStatus::Failed, {}}; }
			Java.Env->DeleteLocalRef(Data);
			return Result;
		}

		void Cancel() override
		{
			FSaveActivity Java;
			if (const jmethodID Method = Java.Method("AndroidThunkJava_WeltallCancelSaveDocument", "()V"))
			{
				Java.Env->CallVoidMethod(Java.Activity, Method);
				Java.Failed();
			}
		}
	};
}

std::shared_ptr<Weltall::ISaveDocuments> MakeAndroidSaveDocuments()
{
	return std::make_shared<FAndroidSaveDocuments>();
}
#endif
