#ifndef _MMCODING_EXPORTS_H_
#define _MMCODING_EXPORTS_H_

#ifdef _MSC_VER
#define MMCODING_EXPORT_DECLSPEC extern "C" __declspec(dllexport)
#define MMCODING_IMPORT_DECLSPEC extern "C" __declspec(dllimport)
#define MMCODING_CLASS_EXPORT_DECLSPEC __declspec(dllexport)
#define MMCODING_CLASS_IMPORT_DECLSPEC __declspec(dllimport)
#endif // _MSC_VER

#ifdef __GNUC__
#define MMCODING_EXPORT_DECLSPEC __attribute__ ((visibility("default")))
#define MMCODING_IMPORT_DECLSPEC
#define MMCODING_CLASS_EXPORT_DECLSPEC __attribute__ ((visibility("default")))
#define MMCODING_CLASS_IMPORT_DECLSPEC 
#endif


#ifdef MMCODING_EXPORTS
#define MMCODING_DECLSPEC MMCODING_EXPORT_DECLSPEC
#define MMCODING_CLASS_DECLSPEC MMCODING_CLASS_EXPORT_DECLSPEC
#else
#define MMCODING_DECLSPEC MMCODING_IMPORT_DECLSPEC
#define MMCODING_CLASS_DECLSPEC MMCODING_CLASS_IMPORT_DECLSPEC
#endif

#endif
