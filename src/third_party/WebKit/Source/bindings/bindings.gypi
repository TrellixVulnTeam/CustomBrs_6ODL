{
    'variables': {
        'bindings_v8_dir': 'v8',
        'blink_output_dir': '<(SHARED_INTERMEDIATE_DIR)/blink',
        'bindings_output_dir': '<(SHARED_INTERMEDIATE_DIR)/blink/bindings',
        'bindings_files': [
            'v8/ActiveDOMCallback.cpp',
            'v8/ActiveDOMCallback.h',
            'v8/ArrayValue.cpp',
            'v8/ArrayValue.h',
            'v8/BindingSecurity.cpp',
            'v8/BindingSecurity.h',
            'v8/CallbackPromiseAdapter.h',
            'v8/CustomElementBinding.cpp',
            'v8/CustomElementBinding.h',
            'v8/CustomElementConstructorBuilder.cpp',
            'v8/CustomElementConstructorBuilder.h',
            'v8/CustomElementWrapper.cpp',
            'v8/CustomElementWrapper.h',
            'v8/DOMDataStore.cpp',
            'v8/DOMDataStore.h',
            'v8/DOMWrapperMap.h',
            'v8/DOMWrapperWorld.cpp',
            'v8/DOMWrapperWorld.h',
            'v8/Dictionary.cpp',
            'v8/Dictionary.h',
            'v8/ExceptionMessages.cpp',
            'v8/ExceptionMessages.h',
            'v8/ExceptionState.cpp',
            'v8/ExceptionState.h',
            'v8/ExceptionStatePlaceholder.cpp',
            'v8/ExceptionStatePlaceholder.h',
            'v8/IDBBindingUtilities.cpp',
            'v8/IDBBindingUtilities.h',
            'v8/NPV8Object.cpp',
            'v8/NPV8Object.h',
            'v8/Nullable.h',
            'v8/PageScriptDebugServer.cpp',
            'v8/PageScriptDebugServer.h',
            'v8/RetainedDOMInfo.cpp',
            'v8/RetainedDOMInfo.h',
            'v8/RetainedObjectInfo.h',
            'v8/ScheduledAction.cpp',
            'v8/ScheduledAction.h',
            'v8/ScopedPersistent.h',
            'v8/ScriptCallStackFactory.cpp',
            'v8/ScriptCallStackFactory.h',
            'v8/ScriptController.cpp',
            'v8/ScriptController.h',
            'v8/ScriptDebugServer.cpp',
            'v8/ScriptDebugServer.h',
            'v8/ScriptEventListener.cpp',
            'v8/ScriptEventListener.h',
            'v8/ScriptFunction.cpp',
            'v8/ScriptFunction.h',
            'v8/ScriptFunctionCall.cpp',
            'v8/ScriptFunctionCall.h',
            'v8/ScriptGCEvent.cpp',
            'v8/ScriptGCEvent.h',
            'v8/ScriptHeapSnapshot.cpp',
            'v8/ScriptHeapSnapshot.h',
            'v8/ScriptObject.cpp',
            'v8/ScriptObject.h',
            'v8/ScriptObjectTraits.h',
            'v8/ScriptPreprocessor.cpp',
            'v8/ScriptPreprocessor.h',
            'v8/ScriptProfiler.cpp',
            'v8/ScriptProfiler.h',
            'v8/ScriptPromise.cpp',
            'v8/ScriptPromise.h',
            'v8/ScriptPromiseResolver.cpp',
            'v8/ScriptPromiseResolver.h',
            'v8/ScriptPromiseResolverWithContext.cpp',
            'v8/ScriptPromiseResolverWithContext.h',
            'v8/ScriptRegexp.cpp',
            'v8/ScriptRegexp.h',
            'v8/ScriptSourceCode.h',
            'v8/ScriptState.cpp',
            'v8/ScriptState.h',
            'v8/ScriptString.cpp',
            'v8/ScriptString.h',
            'v8/ScriptValue.cpp',
            'v8/ScriptValue.h',
            'v8/ScriptWrappable.h',
            'v8/SerializedScriptValue.cpp',
            'v8/SerializedScriptValue.h',
            'v8/SharedPersistent.h',
            'v8/V8AbstractEventListener.cpp',
            'v8/V8AbstractEventListener.h',
            'v8/V8Binding.cpp',
            'v8/V8Binding.h',
            'v8/V8BindingMacros.h',
            'v8/V8Callback.cpp',
            'v8/V8Callback.h',
            'v8/V8CustomElementLifecycleCallbacks.cpp',
            'v8/V8CustomElementLifecycleCallbacks.h',
            'v8/V8DOMActivityLogger.cpp',
            'v8/V8DOMActivityLogger.h',
            'v8/V8DOMConfiguration.cpp',
            'v8/V8DOMConfiguration.h',
            'v8/V8DOMWrapper.cpp',
            'v8/V8DOMWrapper.h',
            'v8/V8ErrorHandler.cpp',
            'v8/V8ErrorHandler.h',
            'v8/V8EventListener.cpp',
            'v8/V8EventListener.h',
            'v8/V8EventListenerList.cpp',
            'v8/V8EventListenerList.h',
            'v8/V8GCController.cpp',
            'v8/V8GCController.h',
            'v8/V8GCForContextDispose.cpp',
            'v8/V8GCForContextDispose.h',
            'v8/V8HiddenValue.cpp',
            'v8/V8HiddenValue.h',
            'v8/V8Initializer.cpp',
            'v8/V8Initializer.h',
            'v8/V8LazyEventListener.cpp',
            'v8/V8LazyEventListener.h',
            'v8/V8MutationCallback.cpp',
            'v8/V8MutationCallback.h',
            'v8/V8NPObject.cpp',
            'v8/V8NPObject.h',
            'v8/V8NPUtils.cpp',
            'v8/V8NPUtils.h',
            'v8/V8NodeFilterCondition.cpp',
            'v8/V8NodeFilterCondition.h',
            'v8/V8ObjectConstructor.cpp',
            'v8/V8ObjectConstructor.h',
            'v8/V8PerContextData.cpp',
            'v8/V8PerContextData.h',
            'v8/V8PerIsolateData.cpp',
            'v8/V8PerIsolateData.h',
            'v8/V8RecursionScope.cpp',
            'v8/V8RecursionScope.h',
            'v8/V8ScriptRunner.cpp',
            'v8/V8ScriptRunner.h',
            'v8/V8StringResource.cpp',
            'v8/V8StringResource.h',
            'v8/V8ThrowException.cpp',
            'v8/V8ThrowException.h',
            'v8/V8ValueCache.cpp',
            'v8/V8ValueCache.h',
            'v8/V8WindowShell.cpp',
            'v8/V8WindowShell.h',
            'v8/V8WorkerGlobalScopeEventListener.cpp',
            'v8/V8WorkerGlobalScopeEventListener.h',
            'v8/WorkerScriptController.cpp',
            'v8/WorkerScriptController.h',
            'v8/WorkerScriptDebugServer.cpp',
            'v8/WorkerScriptDebugServer.h',
            'v8/WrapperTypeInfo.h',
            'v8/custom/V8ArrayBufferCustom.cpp',
            'v8/custom/V8ArrayBufferCustom.h',
            'v8/custom/V8ArrayBufferViewCustom.cpp',
            'v8/custom/V8ArrayBufferViewCustom.h',
            'v8/custom/V8AudioNodeCustom.cpp',
            'v8/custom/V8BlobCustom.cpp',
            'v8/custom/V8BlobCustomHelpers.cpp',
            'v8/custom/V8BlobCustomHelpers.h',
            'v8/custom/V8CSSRuleCustom.cpp',
            'v8/custom/V8CSSStyleDeclarationCustom.cpp',
            'v8/custom/V8CSSValueCustom.cpp',
            'v8/custom/V8CanvasRenderingContext2DCustom.cpp',
            'v8/custom/V8ClientCustom.cpp',
            'v8/custom/V8CryptoCustom.cpp',
            'v8/custom/V8CustomEventCustom.cpp',
            'v8/custom/V8CustomSQLStatementErrorCallback.cpp',
            'v8/custom/V8CustomXPathNSResolver.cpp',
            'v8/custom/V8CustomXPathNSResolver.h',
            'v8/custom/V8DataViewCustom.cpp',
            'v8/custom/V8DataViewCustom.h',
            'v8/custom/V8DedicatedWorkerGlobalScopeCustom.cpp',
            'v8/custom/V8DeviceMotionEventCustom.cpp',
            'v8/custom/V8DeviceOrientationEventCustom.cpp',
            'v8/custom/V8DocumentCustom.cpp',
            'v8/custom/V8ElementCustom.cpp',
            'v8/custom/V8EntryCustom.cpp',
            'v8/custom/V8EntrySyncCustom.cpp',
            'v8/custom/V8ErrorEventCustom.cpp',
            'v8/custom/V8EventCustom.cpp',
            'v8/custom/V8EventTargetCustom.cpp',
            'v8/custom/V8FileCustom.cpp',
            'v8/custom/V8FileReaderCustom.cpp',
            'v8/custom/V8Float32ArrayCustom.h',
            'v8/custom/V8Float64ArrayCustom.h',
            'v8/custom/V8GeolocationCustom.cpp',
            'v8/custom/V8HTMLAllCollectionCustom.cpp',
            'v8/custom/V8HTMLCanvasElementCustom.cpp',
            'v8/custom/V8HTMLCollectionCustom.cpp',
            'v8/custom/V8HTMLDocumentCustom.cpp',
            'v8/custom/V8HTMLElementCustom.cpp',
            'v8/custom/V8HTMLOptionsCollectionCustom.cpp',
            'v8/custom/V8HTMLPlugInElementCustom.cpp',
            'v8/custom/V8HistoryCustom.cpp',
            'v8/custom/V8ImageDataCustom.cpp',
            'v8/custom/V8InjectedScriptHostCustom.cpp',
            'v8/custom/V8InjectedScriptManager.cpp',
            'v8/custom/V8InspectorFrontendHostCustom.cpp',
            'v8/custom/V8Int16ArrayCustom.h',
            'v8/custom/V8Int32ArrayCustom.h',
            'v8/custom/V8Int8ArrayCustom.h',
            'v8/custom/V8JavaScriptCallFrameCustom.cpp',
            'v8/custom/V8LocationCustom.cpp',
            'v8/custom/V8MessageChannelCustom.cpp',
            'v8/custom/V8MessageEventCustom.cpp',
            'v8/custom/V8MessagePortCustom.cpp',
            'v8/custom/V8MutationObserverCustom.cpp',
            'v8/custom/V8NodeCustom.cpp',
            'v8/custom/V8PerformanceEntryCustom.cpp',
            'v8/custom/V8PopStateEventCustom.cpp',
            'v8/custom/V8PromiseCustom.cpp',
            'v8/custom/V8SQLResultSetRowListCustom.cpp',
            'v8/custom/V8SQLTransactionCustom.cpp',
            'v8/custom/V8SQLTransactionSyncCustom.cpp',
            'v8/custom/V8SVGElementCustom.cpp',
            'v8/custom/V8SVGPathSegCustom.cpp',
            'v8/custom/V8ServiceWorkerCustom.cpp',
            'v8/custom/V8StyleSheetCustom.cpp',
            'v8/custom/V8SubtleCryptoCustom.cpp',
            'v8/custom/V8TextCustom.cpp',
            'v8/custom/V8TextTrackCueCustom.cpp',
            'v8/custom/V8TrackEventCustom.cpp',
            'v8/custom/V8TypedArrayCustom.h',
            'v8/custom/V8Uint16ArrayCustom.h',
            'v8/custom/V8Uint32ArrayCustom.h',
            'v8/custom/V8Uint8ArrayCustom.h',
            'v8/custom/V8Uint8ClampedArrayCustom.h',
            'v8/custom/V8WebGLRenderingContextCustom.cpp',
            'v8/custom/V8WebKitPointCustom.cpp',
            'v8/custom/V8WindowCustom.cpp',
            'v8/custom/V8WorkerCustom.cpp',
            'v8/custom/V8WorkerGlobalScopeCustom.cpp',
            'v8/custom/V8XMLHttpRequestCustom.cpp',
            'v8/custom/V8XSLTProcessorCustom.cpp',
            'v8/npruntime.cpp',
            'v8/npruntime_impl.h',
            'v8/npruntime_priv.h',
        ],
        'bindings_unittest_files': [
            'v8/IDBBindingUtilitiesTest.cpp',
            'v8/ScriptPromiseResolverTest.cpp',
            'v8/ScriptPromiseTest.cpp',
        ],
        'conditions': [
            ['OS=="win" and buildtype=="Official"', {
                # On Windows Official release builds, we try to preserve symbol
                # space.
                'bindings_core_generated_aggregate_files': [
                    '<(bindings_output_dir)/V8GeneratedCoreBindings.cpp',
                ],
                'bindings_modules_generated_aggregate_files': [
                    '<(bindings_output_dir)/V8GeneratedModulesBindings.cpp',
                ],
            }, {
                'bindings_core_generated_aggregate_files': [
                    '<(bindings_output_dir)/V8GeneratedCoreBindings01.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings02.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings03.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings04.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings05.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings06.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings07.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings08.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings09.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings10.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings11.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings12.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings13.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings14.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings15.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings16.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings17.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings18.cpp',
                    '<(bindings_output_dir)/V8GeneratedCoreBindings19.cpp',
                ],
                'bindings_modules_generated_aggregate_files': [
                    '<(bindings_output_dir)/V8GeneratedModulesBindings01.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings02.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings03.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings04.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings05.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings06.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings07.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings08.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings09.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings10.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings11.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings12.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings13.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings14.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings15.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings16.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings17.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings18.cpp',
                    '<(bindings_output_dir)/V8GeneratedModulesBindings19.cpp',
                ],
            }],

            # The bindings generator can skip writing generated files if they
            # are identical to the already existing file, which avoids
            # recompilation.  However, a dependency (earlier build step) having
            # a newer timestamp than an output (later build step) confuses some
            # build systems, so only use this on ninja, which explicitly
            # supports this use case (gyp turns all actions into ninja restat
            # rules).
            ['"<(GENERATOR)"=="ninja"', {
              'write_file_only_if_changed': '1',
            }, {
              'write_file_only_if_changed': '0',
            }],
        ],
    },
}