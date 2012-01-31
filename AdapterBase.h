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

#ifndef ADAPTERBASE_H
#define ADAPTERBASE_H

#include <webkit/npapi/npapi.h>
#include <webkit/npapi/npruntime.h>
#include <webkit/npapi/npupp.h>
#if defined(_NPAPI_H_)  // Pre r78002 WebKit defined this.
#include <webkit/npapi/nppalmdefs.h> // Removed from r78002 WebKit.
#endif

#include <glib.h>
#define DLLEXPORT __attribute__ ((visibility("default")))
#define WEAK      __attribute__ ((weak))

#if defined(DEBUG)
#define TRACE(fmt, args...) fprintf(stderr, "TRACE: (%s:%d) %s: " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args)
#elif defined (NDEBUG)
#define TRACE(fmt, args...) (void)0
#else
// Quick fix for broken pdfadapter build: assume release if not otherwise specified.
// ANY project that includes this header needs to define DEBUG or NDEBUG...
#define NDEBUG
#define TRACE(fmt, args...) (void)0
//#error "Don't know if we're debug or release"
#endif


/*
	A luna adapter is basically a browser plugin loaded by luna that renders content 
	from a server running in another process.  For example, the luna Browser app uses an adapter
	to display web pages rendered by BrowserServer, so that the luna process itself does not need to be
	responsible for rendering unknown/untested content from the web.
	
	Writing a Luna adapter consists of two simple steps:
	1: Implement the required C hooks.
	2: Implement a subclass of AdapterBase, defined below.
	
	
	#1: Implement the required C hooks
	----------------------------------
	AdapterBase.cpp includes implementations for all of the standard C entrypoints required by the NPAPI (Netscape Plugin API).
	There are only a few C entrypoints that luna adapter authors need to provide:
	
	
	NPError AdapterLibInitialize(void);
	
	This is just a hook to allow authors a chance to initialize any global internal state when the adapter is 
	loaded and initialized by WebKit.  The implementation may be mostly empty, but it should at least return
	NPERR_NO_ERROR to indicate that the adapter was able to initialize successfully.
	
	
	const char* AdapterGetMIMEDescription(void);	
	
	This simply returns a constant string containing the MIME type for the "content" the adapter will render.
	This is the same as the string app developers will place in the "type" attribute of the HTML <object> element.
	For example, the MIME type for the Browser app's adapter is "application/x-palm-browser", and the corresponding
	object tag in the HTML looks like this:
	<object type="application/x-palm-browser" width="320" height="276" > </object>
	
	
	AdapterBase* AdapterCreate(GMainLoop* mainLoop, int16_t argc, char* argn[], char* argv[]);
	
	Implement this hook to allocate an instance of your AdapterBase subclass (see #2).
	You can perform any required initialization in your subclass constructor, or in this method.
	'mainLoop' contains the GMainLoop that should be used by this adapter if required.  This is
	commonly used with g_io_channels to cooperatively receive IPC messages from the adapter's
	associated server process.  'argn' and 'argv' contain the attribute names & values for
	the instance's corresponding <object> element in the HTML.  Adapter & page authors
	can specify custom attributes which will be passed to the adapter implementation using this mechanism.
	The newly allocated AdapterBase subclass instance should be returned (or NULL if there's some error).
	
	
	uint32_t	AdapterGetMethods(const char*** outNames, const JSMethodPtr **outMethods);
	
	Implement this hook to expose certain static methods to JavaScript code.  
	'outNames' should be set to an array of method names, and outMethods should be set
	to a matching array of function pointers.  The length of the arrays is returned, so
	the trivial implementation simply returns 0 (and the 'out' arguments are then ignored).
	
	
	
	#2: Implementing a subclass of AdapterBase
	------------------------------------------
	The trivial subclass simply implements the pure virtual "handle*" methods by returning 'true' when required.
	This subclass won't actually do much, but it is a valid adapter -- the event handling methods will be called
	for pen & key events that apply to the html <object> element's area, and handlePaint() will be called at least 
	once to draw the adapter's content.
	
	Less trivial adapters typically then enhance handlePaint() to actually draw the content, and call 
	AdapterBase::NPN_InvalidateRect() to request a new paint event when the content needs to be updated.
	The event handling methods can then be meaningfully implemented to provide interactivity.
	
	
	A Few Notes
	-----------
	Most of the NPN_* APIs (which adapter plugins use to call back into webkit to perform various operations) 
	are not available here.  More can be be added if necessary.
	
	Similarly, many of the NPP_* APIs are neither implemented nor exposed by AdapterBase.  If this causes 
	problems for your adapter, then let Jesse know.  At the very least, it's a simple matter to pass the 
	calls through to the subclass implementation so adapter authors at least have the option of implementing 
	them.  However, we don't expect that they will generally be needed.
	
	Questions, suggestions, feature requests, rants, etc., can be sent to Jesse Donaldson.
	
	
	Further Reference:
	------------------
	Here's mozilla's copy of the NPAPI Manual.
	http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/
	
	Here's a good reference page for NPAPI stuff:
	http://developer.mozilla.org/en/docs/Category:NPAPI
	
*/

class AdapterBase;
struct AdapterNPObj;

/**
 *Function signature for exposed JavaScript method calls.
 *	
 * @param adapter  contains the AdapterBase instance for which the method is being called.
 * @param args     List of argument values passed by the JavaScript caller.
 * @param argCount number of NPVariant elements in 'args'.
 * @param result   Set to an NPVariant if the method has a result. String data (if any) must be allocated with NPN_MemAlloc(), and will be freed by the caller.
 * @return NULL on success, or an error string otherwise.
 *
 * If the return value from a JSMethodPtr call is non-null, then an exception will be set on the 
 * adapter's DOM object, and the error string will be visible to the JavaScript programmer.
 */
typedef const char* (*JSMethodPtr)(AdapterBase *adapter, const NPVariant *args, uint32_t argCount, NPVariant *result);


/*
	Adapter authors must implement the following C hooks:
*/
extern "C"
{
	// Called when adapter library is loaded.  Implementation may be empty. Returns NPERR_NO_ERROR on success.
	// There is no adapter instance yet, but the browser APIs have been initialized so static methods like
	// AdapterBase::NPN_GetStringIdentifier() may be used.
	NPError AdapterLibInitialize(void);	
	
	// Should return the content type for this adapter. e.g., "application/x-palm-browser".
	const char* AdapterGetMIMEDescription(void);	
	
	// Should return a new instance of the adapter's AdapterBase subclass. 
	// Arguments are the attribute names & values from the HTML <object> tag.
	// Note that argn and argv are freed after this call has returned, so do not save the pointers and try to access them later.
	AdapterBase* AdapterCreate(NPP instance, GMainLoop* mainLoop, int16_t argc, char* argn[], char* argv[]);
	
	// Allows the adapter to expose methods to JavaScript.
	// Should set outNames and outMethods to matching arrays containing the names 
	// and function pointers of the methods to expose.  Should return the number
	// of methods to expose, or 0 if none.  The given functions will be called when 
	// JavaScript code invokes the named methods on the adapter object in the HTML DOM.
	uint32_t	AdapterGetMethods(const char*** outNames, const JSMethodPtr **outMethods, NPIdentifier **outIDs);
	
}






/**
 * Handles many of the basic mechanics of the NPAPI required to get an 
 * adapter working in luna, and provides a simpler interface to adapter authors.
 */
class AdapterBase {
	
	public:
		AdapterBase(NPP instance, bool cacheAdapter=true, bool useGraphicsContext=false);
		virtual ~AdapterBase(void);
		
		
		/*
			NPN API shims: These allow the adapter implementation to call back into the luna WebKit.
			More will be added as requested.
		*/
		
		// Request a paint event that includes the indicated rectangle.
		// invalidRect is in coordinates relative to the top left of the adapter's area.
		void NPN_InvalidateRect(NPRect *invalidRect);
		
		NPError NPN_GetURL(const char* url, const char* target);

		// Obtain an NPIdentifier for the given string.
		static NPIdentifier NPN_GetStringIdentifier(const char* utf8);

		// Returns the UTF-8 string corresponding to the given string
		// identifier.
		static NPUTF8 *NPN_UTF8FromIdentifier(NPIdentifier identifier);
		
		/**
		 * Release value from a result our output-style variant.
		 * Must be called on the result values of invoked JavaScript methods, since
		 * those NPVariants "own" their values.
		 */
		static void NPN_ReleaseVariantValue(NPVariant *variant);
		
		/***
		 * Accessor for subclasses to get the glory of the netscape funcs
		 */
		static NPNetscapeFuncs *getFuncs();
		
		
		/*
			Utility routines for dealing with NPAPI stuff:
		*/
		
		// Converts given NPString into a regular utf-8 encoded C-style string.
		// Must be deallocated using free() when the caller is finished with it.
		static char* NPStringToString(const NPString& npstring);

		static bool IsIntegerVariant(const NPVariant* variant);
		static int VariantToInteger(const NPVariant* variant);
		static bool IsIntegerVariant(const NPVariant& variant);
		static int VariantToInteger(const NPVariant& variant);
		
		static bool IsDoubleVariant(const NPVariant* variant);
		static double VariantToDouble(const NPVariant* variant);
		static bool IsDoubleVariant(const NPVariant& variant);
		static double VariantToDouble(const NPVariant& variant);
		
		static bool IsBooleanVariant(const NPVariant* variant);
		static bool VariantToBoolean(const NPVariant* variant);
		static bool IsBooleanVariant(const NPVariant& variant);
		static bool VariantToBoolean(const NPVariant& variant);
		
		
		/** 
		 * Invoke a JavaScript method on the adapter's designated event listener, if any.
		 * The event listener should be set by JavaScript code in the "eventListener" 
		 * property of the adapter's DOM object.
		 * This method should be used for invoking app-provided handlers to notify the
		 * app of events that occur for this instance of the adapter.
		 * Except as noted, behaves like InvokeAdapter().
		 */
		bool InvokeEventListener(NPIdentifier methodName,
						  const NPVariant *args, uint32_t argCount, NPVariant *result);
		
		// Invoke the indicated JavaScript method on the adapter's DOM object. 
		// This should be used to call JavaScript-side methods that are part of the
		// the adapter implementation (i.e., not directly exposed to app developers).
		// Returns 'true' if the method was able to be invoked.
		// Note that the method name must first be converted to an NPIdentifier using NPN_GetStringIdentifier().
		// Note also that if the invocation is successful and non-NULL is passed for 'result', 
		// then the caller must call NPN_ReleaseVariantValue() on the resulting NPVariant.
		bool InvokeAdapter(NPIdentifier methodName,
						const NPVariant *args, uint32_t argCount, NPVariant *result);
		
	
		void ThrowException( const char* msg );

	protected:
		NPWindow		mWindow;
		NpPalmWindow	mPalmWindow;
		
		GMainLoop	*mMainLoop;
		NPP			mInstance;
		
		/* This method is called immediately before the call to AdapterGetMethods
		 * if it returns true, AdapterGetMethods is not called at all, and this
		 * method is responsible for method registration. If it returns 0 (which
		 * is the default) it will roll in to AdapterGetMethods. Under no circumstances
		 * will BOTH be called. The calling convention is identical to AdapterGetMethods.
		 */
		virtual uint32_t adapterGetMethods(const char*** outNames, const JSMethodPtr **outMethods, NPIdentifier **outIDs);
		
		/*
			Subclasses must implement these methods to handle pen events.
			NOTE: Unlike NpPalmPenEvents received by NPP_HandleEvent, these 
			pen event coordinates are relative to the top left of the adapter's area.
			
			Currently, we never receive PenMove events from WebKit, so 
			handlePenMove() is not called.
			Return 'true' if the event is handled, false otherwise.
		*/
		virtual bool handlePenDown(NpPalmPenEvent *event) = 0;
		virtual bool handlePenUp(NpPalmPenEvent *event) = 0;
		virtual bool handlePenMove(NpPalmPenEvent *event) = 0;
		virtual bool handlePenClick(NpPalmPenEvent *event) { return false; }
		virtual bool handlePenDoubleClick(NpPalmPenEvent* event) { return false; }
		
		/*
			Subclasses may implement these if they want to handle multitouch events.
			NOTE: In order to recieve multitouch events at all, the class must
			call NPN_SetValue, setting npPalmEnableTouchEvents to true (by
			sending a pointer that is not NULL). Call again with NULL to disable
			multitouch events later if desired. 
			
			NOTE 2: Subclasses of AdapterBase should not call NPN_SetValue directly.
			Instead, they should call getFuncs()->setvalue. 
		*/
		virtual bool handleTouchStart(NpPalmTouchEvent *event);
		virtual bool handleTouchMove(NpPalmTouchEvent *event);
		virtual bool handleTouchEnd(NpPalmTouchEvent *event);
		virtual bool handleTouchCancelled(NpPalmTouchEvent *event);

		/**
		 *  Subclasses may implement these if they want to handle gesture
		 *  events.
		 */
		virtual bool handleGesture(NpPalmGestureEvent *event);
		
		/* 
			Subclasses must implement these methods to handle key events.
			Character encoding is ASCII, despite the comment that says "32-bits to allow any Unicode character".
			
			Return 'true' if the event is handled, false otherwise.
		*/
		virtual bool handleKeyDown(NpPalmKeyEvent *event) = 0;
		virtual bool handleKeyUp(NpPalmKeyEvent *event) = 0;
		
		
		/*
			Paint the indicated area of the screen in plugin-relative coordinates.
			Image data should be written directly to event->dstBuffer, which is 
			already offset to the correct x/y location.
			The implementation can safely assume a scaleFactor of 1.0.
		*/
		virtual void handlePaint(NpPalmDrawEvent* event) = 0;

        /*
            Inform the adapter about the updated window location and size.
        */
		virtual void handleWindowChange(NPWindow* window) {}
	
		/**
		 * Notifies a plug-in instance of a new data stream.
		 */
		virtual NPError handleNewStream(NPMIMEType type, NPStream* stream, NPBool seekable, uint16_t* stype);

		/**
		 * Determines maximum number of bytes that the plug-in can consume.
		 *
		 * @see http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/npp_api17.html#999521
		 */
		virtual int32_t handleWriteReady(NPStream* stream);

		/**
		 * Delivers data to a plug-in instance.
		 *
		 * @see http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/npp_api16.html#999497
		 */
		virtual int32_t handleWrite(NPStream* stream, int32_t offset, int32_t len, void* buffer);

		/**
		 * Tells the plug-in that a stream is about to be closed or destroyed.
		 *
		 * @see http://devedge-temp.mozilla.org/library/manuals/2002/plugin/1.0/npn_api2.html#999376
		 */
		virtual NPError handleDestroyStream(NPStream* stream, NPReason reason);


		/**
		 * Tells the plug-in if the parent window is gaining/loosing focus
		 */
		virtual bool handleFocus(bool focused) { return false;}

		/**
		 * Not a message handler, but part of the message dispatching system
		 * You can override this to have and track additional messages
		 * Same calling convention as the static version
		 */
		virtual bool doPrvObjInvoke(NPObject *obj, NPIdentifier name, const NPVariant *args, uint32_t argCount, NPVariant *result);
		
		/*
			Subclasses may override this if they need to do something when the 
			page's NPP plugin instance is destroyed.  The AdapterBase object itself
			may remain alive after this point, if JavaScript code has a reference to it.
		*/
		virtual void InstanceDestroyed(void);
		
		bool NPN_InvokeDefault(NPObject *obj, const NPVariant *args, uint32_t argCount, NPVariant *result);
		
	static int GetScreenResolution(int& hres, int &vres);

	private:
		AdapterNPObj*	mNPObject; // scriptable object we create to allow JavaScript to make calls on us.
		NPObject*		mDOMObject; // Adapter's object in the page's DOM tree, to allow us to make calls back into JavaScript.
		
		
		static NPClass	sPluginClass;	// Defines implementation methods for mNPObject.
		
		// Info on the methods we expose to JavaScript side via mNPObject.
		NPIdentifier		*mJSIdentifiers;
		const JSMethodPtr	*mJSMethods;
		uint32_t			mJSMethodCount;
		bool                mCacheAdapter; ///< If true this adapter will be cached by WebKit.
		bool                mUseGraphicsContext; ///< If true WebKit will send a PGContext* but no buffer to write into
		
		
		
		void PrvPaint(NpPalmDrawEvent* event);
		JSMethodPtr PrvFindMethod(NPIdentifier name);
		bool PrvInvokeMethod(NPObject *obj, NPIdentifier methodName, const NPVariant *args, uint32_t argCount, NPVariant *result);
		
		// Private NP Plugin Callbacks, 
		// (these translate into appropriate non-static method calls).
		static NPError PrvNPP_New(NPMIMEType pluginType, NPP instance, uint16_t mode, int16_t argc,
						   char* argn[], char* argv[], NPSavedData* saved);
		static NPError PrvNPP_Destroy(NPP instance, NPSavedData** save);
		static NPError PrvNPP_SetWindow(NPP instance, NPWindow* window);
		static NPError PrvNPP_NewStream(NPP instance, NPMIMEType type, NPStream* stream, NPBool seekable, uint16_t* stype);
		static NPError PrvNPP_DestroyStream(NPP instance, NPStream* stream, NPReason reason);
		static void PrvNPP_StreamAsFile(NPP instance, NPStream* stream, const char* fname);
		static int32_t PrvNPP_WriteReady(NPP instance, NPStream* stream);
		static int32_t PrvNPP_Write(NPP instance, NPStream* stream, int32_t offset, int32_t len, void* buffer);
		static void PrvNPP_Print(NPP instance, NPPrint* platformPrint);
		static int16_t PrvNPP_HandleEvent(NPP instance, void* event);
		static void PrvNPP_UrlNotify(NPP instance, const char* url, NPReason reason, void* notifyData);
		static NPError PrvNPP_GetValue(NPP instance, NPPVariable variable, void *retValue);
		static NPError PrvNPP_SetValue(NPP instance, NPNVariable variable, void *retValue);
		
		// Private NP Object callbacks, used for JavaScript integration.  
		// (these translate into appropriate non-static method calls).
		static NPObject* PrvObjAllocate(NPP npp, NPClass* klass);
		static void PrvObjDeallocate(NPObject* obj);
		static void PrvObjInvalidate(NPObject* obj);
		static bool PrvObjHasMethod(NPObject* obj, NPIdentifier name);
		static bool PrvObjInvoke(NPObject *obj, NPIdentifier name, const NPVariant *args, uint32_t argCount, NPVariant *result);
		static bool PrvObjInvokeDefault(NPObject *obj, const NPVariant *args, uint32_t argCount, NPVariant *result);
		static bool PrvObjHasProperty(NPObject *obj, NPIdentifier name);
		static bool PrvObjGetProperty(NPObject *obj, NPIdentifier name, NPVariant *result);
		static bool PrvObjSetProperty(NPObject *obj, NPIdentifier name, const NPVariant *value);
		static bool PrvObjRemoveProperty(NPObject *obj, NPIdentifier name);
		static bool PrvObjEnumerate(NPObject *obj, NPIdentifier **value, uint32_t *count);
		static bool PrvObjConstruct(NPObject *obj, const NPVariant *args, uint32_t argCount, NPVariant *result);

		
	public:

		/**
		 * Creates an NP Object of the given class.
		 * Used by AdapterBase to create a scriptable object visible to JavaScript.
		 */
		NPObject* NPN_CreateObject(NPClass *aClass);
		
		/**
		 * Increment reference count on npobj.
		 */
		static NPObject* NPN_RetainObject(NPObject *npobj);

		static void SetAdapterName( const char* name );
		static void SetAdapterDescription( const char* desc );
		
		/**
		 * Decrement reference count on npobj.
		 * If new count is 0, it may be deallocated (or otherwise garbage collected later).
		 */
		static void NPN_ReleaseObject(NPObject *npobj);

		static void NPN_GetStringIdentifiers(const NPUTF8 **names, int32_t nameCount,
                              NPIdentifier *identifiers);
    
		void* NPN_GetValue(NPNVariable variable);
    
		
		// Only for use internally by C entrypoints in AdapterBase.cpp.
		static void InitializePluginFuncs(NPPluginFuncs* pPluginFuncs);

};


#endif // ADAPTERBASE_H


