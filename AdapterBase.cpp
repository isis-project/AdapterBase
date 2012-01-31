/* @@@LICENSE
*
*      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
LICENSE@@@ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

#include <glib.h>

#include "AdapterBase.h"





/*  
	Outstanding Issues:
	===================
 * pen events (at least move) are passed to browser even when we return true,
 	so dragging across an adapter's view area will cause it to become selected like text,
 	and colored blue.
 * When cursor blinks in a text field outside an adapters area, we receive a paint event
 	for every cursor blink.
 * Invalid rectangles are not prperly respected -- paint events are always for the entire view.

*/


static char g_szAdapterName[512] = "Palm Native Adapter";
static char g_szAdapterDescription[512] = "Palm Adapter to native service";



// -----------------------------------------------------------------------------------

// Holds global pointers to NPN APIs.
// We provide shims in AdapterBase that handle providing the correct instance pointer, etc.
static NPNetscapeFuncs sBrowserFuncs;

static NPIdentifier	sEventListenerID = NULL; // Identifier for finding/invoking methods on the adapter's event listener.

// These values are initially provided in PrvNPP_New(), but since we don't actually 
// instantiate the adapter instance until PrvNPP_SetValue() is called with the main loop,
// we need to temporarily save them here so they can still be passed to AdapterCreate().
// We are guaranteed to receive the PrvNPP_SetValue() call immediately after PrvNPP_New() returns.
static int16_t gArgc = 0;
static char** gArgn = NULL;
static char** gArgv = NULL;



// Structure that defines the methods for our "javascript object".
NPClass AdapterBase::sPluginClass = {
	NP_CLASS_STRUCT_VERSION_CTOR,
 AdapterBase::PrvObjAllocate,
 AdapterBase::PrvObjDeallocate,
 AdapterBase::PrvObjInvalidate,
 AdapterBase::PrvObjHasMethod,
 AdapterBase::PrvObjInvoke,
 AdapterBase::PrvObjInvokeDefault,
 AdapterBase::PrvObjHasProperty,
 AdapterBase::PrvObjGetProperty,
 AdapterBase::PrvObjSetProperty,
 AdapterBase::PrvObjRemoveProperty,
 AdapterBase::PrvObjEnumerate,
 AdapterBase::PrvObjConstruct
};


struct AdapterNPObj {
	NPObject	obj;
	AdapterBase	*adapter;
};


// -----------------------------------------------------------------------------------
// AdapterBase "real" implementation
// -----------------------------------------------------------------------------------
// accessor
NPNetscapeFuncs *AdapterBase::getFuncs()
{
	return &sBrowserFuncs;
}

AdapterBase::AdapterBase(NPP instance, bool cacheAdapter, bool useGraphicsContext) :
				mMainLoop(NULL),
				mInstance(instance),
				mNPObject(NULL),
				mDOMObject(NULL),
				mJSIdentifiers(NULL),
				mJSMethods(NULL),
				mJSMethodCount(0),
				mCacheAdapter(cacheAdapter),
				mUseGraphicsContext(useGraphicsContext)
{
	memset(&mWindow, 0, sizeof(mWindow));
	memset(&mPalmWindow, 0, sizeof(mPalmWindow));
	
	instance->pdata = this;
	
	// Lookup the plugin's DOM object so we can make calls on it using InvokeMethod():
	sBrowserFuncs.getvalue(instance, NPNVPluginElementNPObject, &mDOMObject);
	
}

AdapterBase::~AdapterBase(void)
{
	TRACE("Congratulations, your adapter was not leaked!");
}

void AdapterBase::InstanceDestroyed(void)
{
}

/**
 * Converts a NPString to a "C" style null terminated string.
 *
 * @returns the newly allocated string which the caller is responsible for freeing using ::free().
 */
char* AdapterBase::NPStringToString(const NPString& str)
{
	char* s = (char*) malloc(str.UTF8Length + 1);
	if (str.UTF8Length > 0)
		memcpy(s, str.UTF8Characters, str.UTF8Length);
	
	s[str.UTF8Length] = '\0';
	
	return s;
}

/**
 * A utility function to determine if a variant is or can be converted to an integer type.
 */
bool AdapterBase::IsIntegerVariant(const NPVariant *variant)
{
	return variant != NULL && IsIntegerVariant(*variant);
}

bool AdapterBase::IsIntegerVariant(const NPVariant &variant)
{
	return NPVARIANT_IS_DOUBLE(variant) || NPVARIANT_IS_INT32(variant) || NPVARIANT_IS_BOOLEAN(variant);
}

/**
 * A utility function to convert a variant to an integer type. The caller should determine that
 * this call can succed by first calling IsIntegerVariant.
 *
 * @returns The number (rounded down if type is double) or zero if the variant cannot be converted.
 */
int AdapterBase::VariantToInteger(const NPVariant *variant)
{
	return VariantToInteger(*variant);
}

int AdapterBase::VariantToInteger(const NPVariant &variant)
{
	if (NPVARIANT_IS_INT32(variant))
		return NPVARIANT_TO_INT32(variant);
	else if (NPVARIANT_IS_DOUBLE(variant))
		return static_cast<int>(NPVARIANT_TO_DOUBLE(variant));
	else if (NPVARIANT_IS_BOOLEAN(variant))
		return NPVARIANT_TO_BOOLEAN(variant) ? 1 : 0;
	else
		return 0;
}

/**
 * A utility function to determine if a variant is or can be converted to a double type.
 */
bool AdapterBase::IsDoubleVariant(const NPVariant *variant)
{
	return variant != NULL && IsDoubleVariant(*variant);
}

bool AdapterBase::IsDoubleVariant(const NPVariant& variant)
{
	return NPVARIANT_IS_DOUBLE(variant) || NPVARIANT_IS_INT32(variant) || NPVARIANT_IS_BOOLEAN(variant);
}

/**
 * A utility function to convert a variant to an double type. The caller should determine that
 * this call can succed by first calling IsDoubleVariant.
 *
 * @returns The number or zero if the variant cannot be converted.
 */
double AdapterBase::VariantToDouble(const NPVariant *variant)
{
	return VariantToDouble(*variant);
}

double AdapterBase::VariantToDouble(const NPVariant &variant)
{
	if (NPVARIANT_IS_DOUBLE(variant))
		return NPVARIANT_TO_DOUBLE(variant);
	else if (NPVARIANT_IS_INT32(variant))
		return static_cast<double>(NPVARIANT_TO_INT32(variant));
	else if (NPVARIANT_IS_BOOLEAN(variant))
		return NPVARIANT_TO_BOOLEAN(variant) ? 1.0 : 0.0;
	else
		return 0.0;
}

/**
 * A utility function to determine if a variant is or can be converted to a boolean type.
 */
bool AdapterBase::IsBooleanVariant(const NPVariant *variant)
{
	return variant != NULL && IsBooleanVariant(*variant);
}

bool AdapterBase::IsBooleanVariant(const NPVariant& variant)
{
	return NPVARIANT_IS_DOUBLE(variant) || NPVARIANT_IS_INT32(variant) || NPVARIANT_IS_BOOLEAN(variant);
}

/**
 * A utility function to convert a variant to an boolean type. The caller should determine that
 * this call can succed by first calling IsBooleanVariant.
 *
 * @returns The number or zero if the variant cannot be converted.
 */
bool AdapterBase::VariantToBoolean(const NPVariant *variant)
{
	if (NPVARIANT_IS_BOOLEAN(*variant))
		return NPVARIANT_TO_BOOLEAN(*variant);
	else if (NPVARIANT_IS_INT32(*variant))
		return NPVARIANT_TO_INT32(*variant) != 0;
	else if (NPVARIANT_IS_DOUBLE(*variant))
		return NPVARIANT_TO_DOUBLE(*variant) != 0.0;
	else
		return false;
}
bool AdapterBase::VariantToBoolean(const NPVariant& variant)
{
	return VariantToBoolean(&variant);
}

void AdapterBase::PrvPaint(NpPalmDrawEvent* event)
{
	
	if(mPalmWindow.bpp != 32) {
		fprintf(stderr, "WARNING - %s: rendering only supported in 32 bpp\n", __FUNCTION__);
		return;
	}
	
	/*
	Normally, the source rectangle indicated by event->srcLeft/Top/Right/Bottom should be scaled according to 
	palmWin->scaleFactor, and copied to event->dstBuffer (which is already set to the correct X/Y location).
	But in Luna, adapters will always run at 100% scaling, so we can hide this from subclass implementations.
	*/
	double scaling = mPalmWindow.scaleFactor;

//	printf("Scaling: %f, %lf\n", mPalmWindow.scaleFactor, scaling);
	
	NpPalmDrawEvent event2;
	if(scaling != 1.0) {
		fprintf(stderr, "WARNING - %s: Auto-scaling coordinates to recover from bad scale factor.\n", __FUNCTION__);
		
		// Prevent tragedy in cases where we get funky scaling factor, even though it should always be 1.0.
		event2 = *event;		
		event2.srcLeft = (int32_t)(event2.srcLeft *scaling);
		event2.srcTop = (int32_t)(event2.srcTop *scaling);
		event2.srcRight = (int32_t)(event2.srcRight *scaling);
		event2.srcBottom = (int32_t)(event2.srcBottom *scaling);
		event = &event2;
		
		mPalmWindow.scaleFactor = 1.0;
	}
	
	//TRACE("srcRect: (%d,%d) w:%d, h:%d", event->srcLeft, event->srcTop, event->srcRight - event->srcLeft, event->srcBottom - event->srcTop);
	
	// Call subclass implementation:
	handlePaint(event);
	
	return;
}

/*
	Searches the list of registered method identifiers to see if we
	support the requested method.  Returns the method pointer if so, NULL otherwise.
*/
JSMethodPtr AdapterBase::PrvFindMethod(NPIdentifier name)
{
	// We should be able to assume that the method IDs have been initialized already,
	// since it happens when the scriptable object is allocated in PrvNPP_GetValue().
	
	for(uint32_t i=0; i<mJSMethodCount; i++)
	{
		if(mJSIdentifiers[i] == name)
		{
			return mJSMethods[i];
		}
	}
	
	// If method wasn't found, look it up & print an error to ease debugging.
	// Not really needed. It turns out this legitimately fails a lot, and the 
	// browser prints an error when you try to call a non-existant method.
//	NPUTF8 *nameStr = sBrowserFuncs.utf8fromidentifier(name);
//	TRACE(" Could not find method '%s'.", nameStr ? nameStr : "(null)");
//	sBrowserFuncs.memfree(nameStr);
	
	return NULL;
}

/* 
	Helper function.
	This initializes the NPPluginFuncs structure with pointers to all the static 
	entrypoints WebKit may use to call into the adapter.  We provide static private 
	implementations which take care of calling non-static methods with the appropriate
	AdapterBase instance.
*/

void AdapterBase::InitializePluginFuncs(NPPluginFuncs* pPluginFuncs)
{
	pPluginFuncs->newp           = AdapterBase::PrvNPP_New;
	pPluginFuncs->destroy        = AdapterBase::PrvNPP_Destroy;
	pPluginFuncs->setwindow      = AdapterBase::PrvNPP_SetWindow;
	pPluginFuncs->newstream      = AdapterBase::PrvNPP_NewStream;
	pPluginFuncs->destroystream  = AdapterBase::PrvNPP_DestroyStream;
	pPluginFuncs->asfile         = AdapterBase::PrvNPP_StreamAsFile;
	pPluginFuncs->writeready     = AdapterBase::PrvNPP_WriteReady;
	pPluginFuncs->write          = AdapterBase::PrvNPP_Write;
	pPluginFuncs->print          = AdapterBase::PrvNPP_Print;
	pPluginFuncs->event          = AdapterBase::PrvNPP_HandleEvent;
	pPluginFuncs->urlnotify      = AdapterBase::PrvNPP_UrlNotify;
	pPluginFuncs->javaClass      = 0;
	pPluginFuncs->getvalue       = AdapterBase::PrvNPP_GetValue;
	pPluginFuncs->setvalue       = AdapterBase::PrvNPP_SetValue;
}

bool AdapterBase::InvokeEventListener(NPIdentifier methodName,
								  const NPVariant *args, uint32_t argCount, NPVariant *result)
{
	NPVariant eventListener;
	bool success = false;
	
	// Try to obtain the event listener.
	// We look it up for each call in case it's been changed.
	eventListener.type = NPVariantType_Void;
	success = sBrowserFuncs.getproperty(mInstance, mDOMObject, sEventListenerID, &eventListener);
	
	// We can proceed if there's an event listener...
	if(success)
	{
		// Invoke indicated method on the event listener, as long as the listener's an object:
		if(NPVARIANT_IS_OBJECT(eventListener))
		{
			success = PrvInvokeMethod(NPVARIANT_TO_OBJECT(eventListener), methodName, args, argCount, result);
		}
		else success = false;
		
		// Release our reference on the event listener object:
		NPN_ReleaseVariantValue(&eventListener);
	}
		
	return success;
}

bool AdapterBase::InvokeAdapter(NPIdentifier methodName,
										  const NPVariant *args, uint32_t argCount, NPVariant *result)
{
	return PrvInvokeMethod(mDOMObject, methodName, args, argCount, result);
}

/**
 * Throw a JavaScript exception.
 *
 * @The exception message to set.
 */
void AdapterBase::ThrowException(const char* msg)
{
	sBrowserFuncs.setexception(mDOMObject, msg);
}

bool AdapterBase::PrvInvokeMethod(NPObject *obj, NPIdentifier methodName,
							 const NPVariant *args, uint32_t argCount, NPVariant *result)
{
	NPVariant localResult;
	bool success = false;
	
	localResult.type = NPVariantType_Void;
	success = sBrowserFuncs.invoke(mInstance, obj, methodName, args, argCount, &localResult);
	
	// Return method invocation result to the caller, if desired.
	// Or release the value if the caller doesn't want it.
	if(success)
	{
		if(result != NULL)
			*result = localResult;
		else
			NPN_ReleaseVariantValue(&localResult);
	}
	
	return success;
}



// -----------------------------------------------------------------------------------
// Utility routines for saving argn & argv.
// We can't use g_strdupv() since the arrays are apparently not null terminated.
// -----------------------------------------------------------------------------------

char** PrvDupArgv(int argc, char**argv) {
	if(argc == 0 || argv == NULL) 
		return NULL;
	
	char **newargs = g_new0(char*, argc+1);
	
	for(int i=0; i<argc; i++)
	{
		newargs[i] = g_strdup(argv[i]);
	}
	
	return newargs;
}

void PrvFreeArgv(char**argv) 
{g_strfreev(argv);}


// -----------------------------------------------------------------------------------
// NPAPI Exported Entrypoints.
// These 4 C hooks are the only ones we need to export from the adapter library.
// They are described in AdapterBase.h.
// All the rest are handled via procedure pointers, configured in NP_Initialize().
// -----------------------------------------------------------------------------------

/**
 * Return the MIME type of this plugin.
 *
 * @see http://gplflash.sourceforge.net/gplflash2_blog/npapi.html#SEC12
 */
extern "C" DLLEXPORT
char* NP_GetMIMEDescription()
{
	return const_cast<char*>(AdapterGetMIMEDescription());
}

extern "C" DLLEXPORT
NPError NP_Initialize(NPNetscapeFuncs* pBrowserFuncs, NPPluginFuncs* pPluginFuncs)
{
	memcpy(&sBrowserFuncs, pBrowserFuncs, sizeof(NPNetscapeFuncs));
	AdapterBase::InitializePluginFuncs(pPluginFuncs);
	
	// We only need to look up this identifier once, so it's static.
	// It's used in InvokeMethod(), to send method calls to the adapter's
	// designated event listener if available.
	sEventListenerID = AdapterBase::NPN_GetStringIdentifier("eventListener");
	
	return AdapterLibInitialize();
}

extern "C" DLLEXPORT
NPError NP_Shutdown(void)
{
	return NPERR_GENERIC_ERROR;
}

/**
 * @see https://developer.mozilla.org/en/NP_GetValue.
 */
extern "C" DLLEXPORT
NPError NP_GetValue(void *future, NPPVariable aVariable, void *aValue)
{
	NPError err = NPERR_NO_ERROR;

	switch (aVariable) {
		case NPPVpluginNameString:
			*static_cast<char**>(aValue) = g_szAdapterName;
			break;
			
		case NPPVpluginDescriptionString:
			*static_cast<char**>(aValue) = g_szAdapterDescription;
			break;

		default:
			err = NPERR_INVALID_PARAM;
	}
	
	return err;
}


// -----------------------------------------------------------------------------------
// Static implementations of the NPP_* APIs.
// These are the C entrypoints typically required to be implemented by a browser 
// plugin. They're provided to the browser as procedure pointers, through NP_Initialize().
// -----------------------------------------------------------------------------------

/*
	Called by WebKit to allocate a new instance of the adapter.
	Since we'd like a GMainLoop at initialization time, we defer the actual instantiation until
	we have the loop.
*/
NPError AdapterBase::PrvNPP_New(NPMIMEType pluginType, NPP instance, uint16_t mode, int16_t argc,
		char* argn[], char* argv[], NPSavedData* saved)
{
	
	// We don't actually create the Adapter instance here, since the GMainLoop hasn't been set yet,
	// and this is usually very helpful to have at initialization time.  So, we just set pData to NULL,
	// and then wait until the GMainLoop is set to actually create the instance.
	// This is guaranteed to happen immediately after NPP_New() returns.
	
	instance->pdata = NULL;
	
	gArgc = argc;
	gArgn = PrvDupArgv(argc, argn);
	gArgv = PrvDupArgv(argc, argv);
			
	
	return NPERR_NO_ERROR;
}


/*
	Called when the page is closed, or the plugin instance is for some other reason no longer needed.
	Deletes the AdapterBase subclass instance, or just marks it as would-be-deleted if the JavaScript side still has references to it.
*/
NPError AdapterBase::PrvNPP_Destroy(NPP instance, NPSavedData** save)
{
	
	if(instance->pdata == NULL) {
		fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
		return NPERR_GENERIC_ERROR;
	}
	
	AdapterBase* a = (AdapterBase*) instance->pdata;
	if(a != NULL) {
		
		// Call the virtual destroy method for adapters that want to do things like 
		// disconnect from their servers when the plugin is destroyed, even though the 
		// JavaScript system may still hold references to the object and keep it alive.
		a->InstanceDestroyed();
		
		// Release the reference the plugin instance was holding on the JS object, if any.
		// Sometimes, this will cause AdapterBase::PrvObjDeallocate() to be called also.
		if(a->mNPObject != NULL)
			NPN_ReleaseObject(&a->mNPObject->obj);
		
		a->mInstance = NULL;

		if (a->mDOMObject != NULL)
			NPN_ReleaseObject(a->mDOMObject);
		
		a->mDOMObject = NULL;
		
		// if there's no live javascript references to the adapter, then delete it.
		// If it's not deleted here, then it should happen in PrvObjectDeallocate().
		if(a->mNPObject == NULL) {
			delete a;
		}
	}
	
	instance->pdata = NULL;
	
	return NPERR_NO_ERROR;
}

/*
	Called before we receive a paint event, and any time our "window" (area on the page) changes.
	TODO: Bug? Currently seems to be called TWICE before each paint event.
*/
NPError AdapterBase::PrvNPP_SetWindow(NPP instance, NPWindow* window)
{
    if(instance->pdata == NULL) {
        fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
        return NPERR_GENERIC_ERROR;
    }
    
    AdapterBase* a = (AdapterBase*) instance->pdata;
    if (!a) {
        fprintf(stderr, "WARNING - %s no adapter base.\n", __FUNCTION__); 
        return NPERR_GENERIC_ERROR;
    }
        if (window==NULL || window->window == NULL) {
          fprintf(stderr, "Window is null\n");
          return NPERR_GENERIC_ERROR;
    }
    
    NpPalmWindow* palmWin = (NpPalmWindow*) window->window;
    if (!palmWin)
        return NPERR_NO_ERROR;
                
	double scaling = palmWin->scaleFactor;
	
	if(scaling != 1.0) {
		fprintf(stderr, "WARNING: AdapterBase::PrvNPP_SetWindow(): Scaling factor is %f, 1.0 expected.\n"
				"Do you have 'EnableFitWidth=false' in your /etc/palm/browser.conf?\n", (float)scaling);
	}

	// only update data if its changed from the old settings, since
	// WebKit sometimes calls NPP_SetWindow when nothing has changed
	if (a->mWindow.x != window->x ||
		a->mWindow.y != window->y ||
		a->mWindow.width != window->width ||
		a->mWindow.height != window->height ||
		a->mPalmWindow.visible != palmWin->visible ||
		a->mPalmWindow.scaleFactor != palmWin->scaleFactor) {

		a->mWindow = *window;
		a->mPalmWindow = *palmWin;
		a->mWindow.window = &a->mPalmWindow;
		
		// TRACE("Set window to (%ld,%ld) w:%lu, h:%lu", window->x, window->y, window->width, window->height);

		a->handleWindowChange(window);
	}
	
	return NPERR_NO_ERROR;
}

NPError AdapterBase::handleNewStream(NPMIMEType type, NPStream* stream, NPBool seekable, uint16_t* stype)
{
	if (stype)
		*stype = NP_NORMAL;
	return NPERR_NO_ERROR;
}

int32_t AdapterBase::handleWriteReady(NPStream* stream)
{
	// We don't accept streamed data, but if we return 0 here, then the browser will try to resend data at intervals,
	// to see if we're ready yet.  So, we return a small number, and then cause the stream to be destroyed when the
	// write actually happens by returning a negative number there.  This way, everything is cleaned up.
	return 128;
}

/**
 * Delivers data to a plug-in instance.
 */
int32_t AdapterBase::handleWrite(NPStream* stream, int32_t offset, int32_t len, void* buffer)
{
	return -1; // destroy the stream.
}

/**
 * Tells the plug-in that a stream is about to be closed or destroyed.
 */
NPError AdapterBase::handleDestroyStream(NPStream* stream, NPReason reason)
{
	return NPERR_NO_ERROR;
}

/**
 * Notifies a plug-in instance of a new data stream.
 */
NPError AdapterBase::PrvNPP_NewStream(NPP instance, NPMIMEType type, NPStream* stream, NPBool seekable, uint16_t* stype)
{
	if(instance->pdata == NULL) {
        fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
        return NPERR_GENERIC_ERROR;
    }
    
	return static_cast<AdapterBase*>(instance->pdata)->handleNewStream(type, stream, seekable, stype);
}

/**
 * Tells the plug-in that a stream is about to be closed or destroyed.
 *
 * @see http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/npp_api3.html#999152
 */
NPError AdapterBase::PrvNPP_DestroyStream(NPP instance, NPStream* stream, NPReason reason)
{
	if(instance->pdata == NULL) {
        fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
        return NPERR_GENERIC_ERROR;
    }
    
	return static_cast<AdapterBase*>(instance->pdata)->handleDestroyStream(stream, reason);
}

/*	Unimplemented. */
void AdapterBase::PrvNPP_StreamAsFile(NPP instance, NPStream* stream, const char* fname)
{ return;}


/**
 * Determines maximum number of bytes that the plug-in can consume.
 *
 * @see http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/npp_api17.html#999521
 */
int32_t AdapterBase::PrvNPP_WriteReady(NPP instance, NPStream* stream)
{
	if(instance->pdata == NULL) {
        fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
        return 128; // we can receive a small amount of data at a time.
    }
    
	return static_cast<AdapterBase*>(instance->pdata)->handleWriteReady(stream);
}


/**
 * Delivers data to a plug-in instance.
 *
 * @see http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/npp_api16.html#999497
 */
int32_t AdapterBase::PrvNPP_Write(NPP instance, NPStream* stream, int32_t offset, int32_t len, void* buffer)
{
	if(instance->pdata == NULL) {
        fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
        return -1; // destroys the stream
    }
    
	return static_cast<AdapterBase*>(instance->pdata)->handleWrite(stream, offset, len, buffer);
}

/*	Unimplemented. */
void AdapterBase::PrvNPP_Print(NPP instance, NPPrint* platformPrint)
{ }


/*
	Called by webkit to pass all manner of events into the adapter.
	We do minimal error checking, a bit of massaging in some cases,
	and pass the events on to pure virtual handler methods.
*/
int16_t AdapterBase::PrvNPP_HandleEvent(NPP instance, void* event)
{
	
	if(instance->pdata == NULL) {
		fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
		return false;
	}
	
	AdapterBase *a = (AdapterBase*)instance->pdata;
	
	
	int16_t result = false;
	NPEvent* npEvent = (NPEvent*) event;
	if (!npEvent) {
		return 0;
	}
	
	NpPalmPenEvent penEvent;

//	TRACE("type=%d", (int)npEvent->eventType);
	
	switch (npEvent->eventType) {
	case (npPalmDrawEvent): {
		a->PrvPaint(&(npEvent->data.drawEvent));
		result = true;
		break;
	}
	case (npPalmPenDownEvent): {
		penEvent = npEvent->data.penEvent;
		penEvent.xCoord -= a->mWindow.x;
		penEvent.yCoord -= a->mWindow.y;
		
		result = a->handlePenDown(&penEvent);
		break;
	}
	case (npPalmPenMoveEvent): {
		penEvent = npEvent->data.penEvent;
		penEvent.xCoord -= a->mWindow.x;
		penEvent.yCoord -= a->mWindow.y;
		
		result = a->handlePenMove(&penEvent);
		break;
	}
	case (npPalmPenUpEvent): {
		penEvent = npEvent->data.penEvent;
		penEvent.xCoord -= a->mWindow.x;
		penEvent.yCoord -= a->mWindow.y;
		
		result = a->handlePenUp(&penEvent);
		break;
	}
	case (npPalmPenClickEvent): {
		penEvent = npEvent->data.penEvent;
		penEvent.xCoord -= a->mWindow.x;
		penEvent.yCoord -= a->mWindow.y;
		
		result = a->handlePenClick(&penEvent);
		break;
	}
	case (npPalmPenDoubleClickEvent): {
		penEvent = npEvent->data.penEvent;
		penEvent.xCoord -= a->mWindow.x;
		penEvent.yCoord -= a->mWindow.y;
		
		result = a->handlePenDoubleClick(&penEvent);
		break;
	}
	case (npPalmKeyDownEvent): {
		result = a->handleKeyDown(&npEvent->data.keyEvent);
		break;
	}
	case (npPalmKeyUpEvent): {
		result = a->handleKeyUp(&npEvent->data.keyEvent);
		break;
	}
	case (npPalmTouchStartEvent): {
		result = a->handleTouchStart(&npEvent->data.touchEvent);
		break;
	}
	case (npPalmTouchMoveEvent): {
		result = a->handleTouchMove(&npEvent->data.touchEvent);
		break;
	}
	case (npPalmTouchEndEvent): {
		result = a->handleTouchEnd(&npEvent->data.touchEvent);
		break;
	}
	case (npPalmTouchCancelledEvent): {
		result = a->handleTouchCancelled(&npEvent->data.touchEvent);
		break;
	}
	case (npPalmGestureEvent): {
		NpPalmGestureEvent gestureEvent = npEvent->data.gestureEvent;
		gestureEvent.x -= a->mWindow.x;
		gestureEvent.y -= a->mWindow.y;
		gestureEvent.center_x -= a->mWindow.x;
		gestureEvent.center_y -= a->mWindow.y;
		result = a->handleGesture(&gestureEvent);
		break;
	}
	case (npPalmSystemEvent): {
		NpPalmSystemEvent systemEvent = npEvent->data.systemEvent;
		switch (systemEvent.type) {
		case (npPalmGainFocusEvent):
			result = a->handleFocus(true);
			break;
		case (npPalmLoseFocusEvent):
			result = a->handleFocus(false);
			break;
		default:
			break;
		}
	}
	default:
		break;
	}

//	TRACE("returning handled=%d", (int)result);

	return result;
}

/*	Unimplemented. */
void AdapterBase::PrvNPP_UrlNotify(NPP instance, const char* url, NPReason reason, void* notifyData)
{ return;}

/*
	Called by webkit to look up instance specific property values.
	Currently used only for scripting integration.
*/
NPError AdapterBase::PrvNPP_GetValue(NPP instance, NPPVariable variable, void *retValue)
{
	AdapterBase *a = (AdapterBase*)instance->pdata;
	const char* *names = NULL;
	NPObject *obj = NULL;
	
	if (a == NULL)
		return NPERR_INVALID_PARAM;
	
	bool handled = true;
	
	switch (static_cast<int>(variable)) {
		case NPPVpluginNameString:
			static const char* sName = "Palm Native Adapter";
			*((char**) retValue) = const_cast<char*>(sName);
			break;
			
		case NPPVpluginDescriptionString:
			static const char* sDesc = "Palm Adapter to native service";
			*((char**) retValue) = const_cast<char*>(sDesc);
			break;
			
		case NPPVpluginScriptableNPObject:
			
			// start with the member version
			a->mJSMethodCount = a->adapterGetMethods(&names, &a->mJSMethods, &a->mJSIdentifiers);
			
			// if the internal had nothing to say, use the c-api version
			if ( a->mJSMethodCount <= 0 )
			{
				a->mJSMethodCount = AdapterGetMethods(&names, &a->mJSMethods, &a->mJSIdentifiers);
			}
			
			// If we're not scriptable, then we fail to return the "scriptable object" value to the host browser.
			if(a->mJSMethodCount <= 0 || a->mJSIdentifiers == NULL || names == NULL || a->mJSMethods == NULL) {
				handled = false;
				break;
			}
			
			// If the method IDs array hasn't been filled out yet, then do that:
			if(a->mJSIdentifiers[0] == NULL)
				sBrowserFuncs.getstringidentifiers(names, a->mJSMethodCount, a->mJSIdentifiers);
			
			// Create the "scriptable object" for this instance:
			if (a->mNPObject == NULL)
				obj = a->NPN_CreateObject(&sPluginClass);
			
			// The return value from NPN_CreateObject should be the same 
			// as the value returned by our PrvObjAllocate() implementation.
			if(obj != (NPObject*)(a->mNPObject)){
				fprintf(stderr, "obj != a->mNPObject, go check everything!");
			}
			
			a->NPN_RetainObject((NPObject*)a->mNPObject);
			*((NPObject**) retValue) = (NPObject*)a->mNPObject;
			break;
		case npPalmCachePluginValue:
			*reinterpret_cast<bool*>(retValue) = a->mCacheAdapter;
			break;
		case npPalmUseGraphicsContext:
		    *reinterpret_cast<bool*>(retValue) = a->mUseGraphicsContext;
		    break;
		default:
			handled = false;
	}

	if (!handled)
		return NPERR_INVALID_PARAM;

	return NPERR_NO_ERROR;
}
	
uint32_t AdapterBase::adapterGetMethods(const char*** outNames, const JSMethodPtr **outMethods, NPIdentifier **outIDs)
{
	return 0;
}

/*
	Called by webkit to inform the plugin instance about the GMainLoop it will be running under.
	Probably called in other instances too, but we ignore them for now. ;-D
*/
NPError AdapterBase::PrvNPP_SetValue(NPP instance, NPNVariable variable, void *value)
{
//	TRACE("type=%d", variable);
	
	if (!instance) {
		return NPERR_INVALID_PARAM;
	}
	
	// We must allow a NULL adapter instance when setting the event loop,
	// since that's actually when we *allocate* the adapter instance.
	if(instance->pdata == NULL && variable != (NPNVariable)npPalmEventLoopValue) {
		fprintf(stderr, "WARNING - %s called with NULL adapter instance.\n", __FUNCTION__); 
		return NPERR_GENERIC_ERROR;
	}
	
	
	switch (static_cast<int>(variable)) {
	case (npPalmEventLoopValue): {
		
		GMainLoop* mainLoop = (GMainLoop*) value;
		AdapterBase* a;
		
		// Event loop has been set, so create the Adapter instance if it hasn't been created already.
		if(instance->pdata == NULL) {
			a = AdapterCreate(instance, mainLoop, gArgc, gArgn, gArgv);
			if (a == NULL) {
				return NPERR_GENERIC_ERROR;
			}
			
			// free saved arguments:
			gArgc = 0;
			PrvFreeArgv(gArgn);
			PrvFreeArgv(gArgv);
			gArgn = NULL;
			gArgv = NULL;
		}
		
		// Set the loop in the instance too:
		a = (AdapterBase*)instance->pdata;
		a->mMainLoop = mainLoop;
		
		break;
	}
	default:
		break;
	}
	
	return NPERR_NO_ERROR;
}


int AdapterBase::GetScreenResolution(int& hres, int &vres)
{
	hres = 320;
	vres = 480;

	int err(0);

#if defined(__arm__)
    int fbfd(-1);
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    fbfd = open("/dev/fb0", O_RDWR);
    if (-1 == fbfd) {
        TRACE("Error: cannot open framebuffer device");
		err = errno;
    }

    // Get fixed screen information
	if (!err) {
		err = ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
		if (err) {
			TRACE("Error reading fixed info.");
		}
	}

    // Get variable screen information
	if (!err) {
		err = ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
		if (err) {
			TRACE("Error reading variable info.");
		}
		else {
			TRACE("%dx%d, %dbpp", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
			hres = vinfo.xres;
			vres = vinfo.yres;
		}
	}

	if (fbfd != -1) {
    	close(fbfd);
	}
#endif

	return err;
}


// -----------------------------------------------------------------------------------
// Static NPN Object hook implementations.
// These are set up to expose AdapterBase objects as scriptable JavaScript objects.
// This handles generic method dispatch, and management of the AdapterBase object lifetime.
// The actual behavior of particular methods is of course handled by the adapter author.
// -----------------------------------------------------------------------------------

/*
	Called to allocate the scriptable object for an adapter plugin instance
*/
NPObject* AdapterBase::PrvObjAllocate(NPP npp, NPClass* klass)
{
	// We allocate only a small placeholder object for the NPObject required for JS scripting integration.
	// This complicates things slightly in some ways, since this separate object is reference counted & 
	// garbage collected, whereas the plugin instance is explicitly deleted.  We set things up so that the
	// lifetime of the AdapterBase is whichever is longer, and then we can route all NSObject scripting related 
	// calls to the AdapterBase, providing a unified interface for adapter authors.
	
	AdapterBase* a = (AdapterBase*) npp->pdata;
	if(a == NULL) return NULL;
	
	
	// This should be safe since AFAIK only we can allocate NPObjects of our class.
	a->mNPObject = (AdapterNPObj*)malloc(sizeof(AdapterNPObj));
	a->mNPObject->adapter = a;
	
	return (NPObject*)a->mNPObject;
}

/*
	Called to deallocate the scriptable object assiciated with a plugin instance, when its refcount reaches zero.
	(Or else sometime after that, when the object is garbage collected).
*/
void AdapterBase::PrvObjDeallocate(NPObject* obj)
{
	AdapterBase		*a = ((AdapterNPObj*)obj)->adapter;
	
	if(obj != (NPObject*)a->mNPObject) {
		// This should never happen.
		fprintf(stderr, "WARNING: AdapterBase::PrvObjDeallocate: obj != a->mNPObject, adapter may be leaked.\n");
		return;
	}
	
	// First deallocate our little placeholder object, and clear out the reference to it:
	free(obj);
	a->mNPObject = NULL;
	
	// Next, if the plugin instance has already been destroyed, 
	// then we can deallocate the AdapterBase as well.
	// If it's not deleted here, then it should happen in PrvNPP_Destroy().
	if(a->mInstance == NULL)
	{
		delete a;
	}
	
	return;
}

void AdapterBase::PrvObjInvalidate(NPObject* obj)
{fprintf(stderr, "AdapterBase::PrvObjInvalidate: UNIMPLEMENTED\n"); }


/**
 * Called by webkit to find out if our object has a native implementation for the given method.
 */
bool AdapterBase::PrvObjHasMethod(NPObject* obj, NPIdentifier name)
{
	AdapterBase		*a = ((AdapterNPObj*)obj)->adapter;
	return a->PrvFindMethod(name) != NULL;
}

/**
 * Called by webkit to invoke a native method on our object from JavaScript.
 */
bool AdapterBase::PrvObjInvoke(NPObject *obj, NPIdentifier name,
					 const NPVariant *args, uint32_t argCount, NPVariant *result)
{
	AdapterBase		*a = ((AdapterNPObj*)obj)->adapter;
	return a->doPrvObjInvoke(obj, name, args, argCount, result);
}

bool AdapterBase::doPrvObjInvoke(NPObject *obj, NPIdentifier name, const NPVariant *args, uint32_t argCount, NPVariant *result)
{	
	// See if we implement this method:
	JSMethodPtr method = PrvFindMethod(name);
	if(method == NULL) return false;
	
	// If so, call it.
	const char* errorMsg = (method)(this, args, argCount, result);
	
	// Set exception & report error message if something bad happened.
	if(errorMsg != NULL)
		sBrowserFuncs.setexception(mDOMObject, errorMsg);
	
	return true;
}

bool AdapterBase::PrvObjInvokeDefault(NPObject *obj, const NPVariant *args,
							uint32_t argCount, NPVariant *result)
{fprintf(stderr, "AdapterBase::PrvObjInvokeDefault: UNIMPLEMENTED\n"); return false;}

bool AdapterBase::PrvObjHasProperty(NPObject *obj, NPIdentifier name)
{
	// We don't support any properties.
	return false;
}

bool AdapterBase::PrvObjGetProperty(NPObject *obj, NPIdentifier name, NPVariant *result)
{fprintf(stderr, "AdapterBase::PrvObjGetProperty: UNIMPLEMENTED\n"); return false;}

bool AdapterBase::PrvObjSetProperty(NPObject *obj, NPIdentifier name, const NPVariant *value)
{fprintf(stderr, "AdapterBase::PrvObjSetProperty: UNIMPLEMENTED\n"); return false;}

bool AdapterBase::PrvObjRemoveProperty(NPObject *obj, NPIdentifier name)
{fprintf(stderr, "AdapterBase::PrvObjRemoveProperty: UNIMPLEMENTED\n"); return false;}

bool AdapterBase::PrvObjEnumerate(NPObject *obj, NPIdentifier **value, uint32_t *count)
{fprintf(stderr, "AdapterBase::PrvObjEnumerate: UNIMPLEMENTED\n"); return false;}

bool AdapterBase::PrvObjConstruct(NPObject *obj, const NPVariant *args, 
								  uint32_t argCount, NPVariant *result)
{fprintf(stderr, "AdapterBase::PrvObjConstruct: UNIMPLEMENTED\n"); return false;}
   
   



// -----------------------------------------------------------------------------------
// NPN API Shims
// These are local equivalents of the browser-implemented NPN APIs.
// They handle passing in the appropriate NPP instance when needed, 
// and provide more API-like calling behavior than directly accessing sBrowserFuncs.
// For Luna 2.0, we plan to have only a single adapter implementation on the native side,
// which services can interact with to display their data, and this goal is eased somewhat
// by limiting current adapter implementations to critical APIs only.
// -----------------------------------------------------------------------------------

void AdapterBase::NPN_InvalidateRect(NPRect *invalidRect)
{ sBrowserFuncs.invalidaterect(mInstance, invalidRect); }

NPIdentifier AdapterBase::NPN_GetStringIdentifier(const char* utf8)
{ return sBrowserFuncs.getstringidentifier(utf8); }

void AdapterBase::NPN_GetStringIdentifiers(const NPUTF8 **names, int32_t nameCount,
                              NPIdentifier *identifiers)
{ return sBrowserFuncs.getstringidentifiers(names, nameCount, identifiers); }

NPUTF8 *AdapterBase::NPN_UTF8FromIdentifier(NPIdentifier identifier)
{
	return sBrowserFuncs.utf8fromidentifier(identifier);
}

NPObject* AdapterBase::NPN_CreateObject(NPClass *aClass)
{ return sBrowserFuncs.createobject(mInstance, aClass);}

NPObject* AdapterBase::NPN_RetainObject(NPObject *npobj)
{ return sBrowserFuncs.retainobject(npobj);}

void AdapterBase::NPN_ReleaseObject(NPObject *npobj)
{ return sBrowserFuncs.releaseobject(npobj);}

void AdapterBase::NPN_ReleaseVariantValue(NPVariant *variant)
{ return sBrowserFuncs.releasevariantvalue(variant);}

bool AdapterBase::NPN_InvokeDefault(NPObject *obj, const NPVariant *args, uint32_t argCount, NPVariant *result)
{ return sBrowserFuncs.invokeDefault(mInstance, obj, args, argCount, result); }

void* AdapterBase::NPN_GetValue(NPNVariable variable)
{
    void* res = 0;

    if (sBrowserFuncs.getvalue(mInstance, variable, &res) != NPERR_NO_ERROR)
        return 0;

    return res;
}

NPError AdapterBase::NPN_GetURL(const char* url, const char* target)
{
	return sBrowserFuncs.geturl(mInstance, url, target);
}

void AdapterBase::SetAdapterName( const char* name )
{
	if (name != NULL) {
		strncpy( g_szAdapterName, name, sizeof(g_szAdapterName)-1 );
		g_szAdapterName[sizeof(g_szAdapterName)-1] = '\0';
	}
}

void AdapterBase::SetAdapterDescription( const char* desc )
{
	if (desc != NULL) {
		strncpy( g_szAdapterDescription, desc, sizeof(g_szAdapterDescription)-1 );
		g_szAdapterDescription[sizeof(g_szAdapterDescription)-1] = '\0';
	}
}

// stubs for multitouch support. 
bool AdapterBase::handleTouchStart(NpPalmTouchEvent *event) { return true; }
bool AdapterBase::handleTouchMove(NpPalmTouchEvent *event) { return true; }
bool AdapterBase::handleTouchEnd(NpPalmTouchEvent *event) { return true; }
bool AdapterBase::handleTouchCancelled(NpPalmTouchEvent *event) { return true; }

bool AdapterBase::handleGesture(NpPalmGestureEvent *event) { return true; }
