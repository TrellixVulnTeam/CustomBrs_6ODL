// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_H_
#define V8_DEBUG_H_

#include "allocation.h"
#include "arguments.h"
#include "assembler.h"
#include "execution.h"
#include "factory.h"
#include "flags.h"
#include "frames-inl.h"
#include "hashmap.h"
#include "platform.h"
#include "string-stream.h"
#include "v8threads.h"

#include "../include/v8-debug.h"

namespace v8 {
namespace internal {


// Forward declarations.
class EnterDebugger;


// Step actions. NOTE: These values are in macros.py as well.
enum StepAction {
  StepNone = -1,  // Stepping not prepared.
  StepOut = 0,   // Step out of the current function.
  StepNext = 1,  // Step to the next statement in the current function.
  StepIn = 2,    // Step into new functions invoked or the next statement
                 // in the current function.
  StepMin = 3,   // Perform a minimum step in the current function.
  StepInMin = 4  // Step into new functions invoked or perform a minimum step
                 // in the current function.
};


// Type of exception break. NOTE: These values are in macros.py as well.
enum ExceptionBreakType {
  BreakException = 0,
  BreakUncaughtException = 1
};


// Type of exception break. NOTE: These values are in macros.py as well.
enum BreakLocatorType {
  ALL_BREAK_LOCATIONS = 0,
  SOURCE_BREAK_LOCATIONS = 1
};


// The different types of breakpoint position alignments.
// Must match Debug.BreakPositionAlignment in debug-debugger.js
enum BreakPositionAlignment {
  STATEMENT_ALIGNED = 0,
  BREAK_POSITION_ALIGNED = 1
};


// Class for iterating through the break points in a function and changing
// them.
class BreakLocationIterator {
 public:
  explicit BreakLocationIterator(Handle<DebugInfo> debug_info,
                                 BreakLocatorType type);
  virtual ~BreakLocationIterator();

  void Next();
  void Next(int count);
  void FindBreakLocationFromAddress(Address pc);
  void FindBreakLocationFromPosition(int position,
      BreakPositionAlignment alignment);
  void Reset();
  bool Done() const;
  void SetBreakPoint(Handle<Object> break_point_object);
  void ClearBreakPoint(Handle<Object> break_point_object);
  void SetOneShot();
  void ClearOneShot();
  bool IsStepInLocation(Isolate* isolate);
  void PrepareStepIn(Isolate* isolate);
  bool IsExit() const;
  bool HasBreakPoint();
  bool IsDebugBreak();
  Object* BreakPointObjects();
  void ClearAllDebugBreak();


  inline int code_position() {
    return static_cast<int>(pc() - debug_info_->code()->entry());
  }
  inline int break_point() { return break_point_; }
  inline int position() { return position_; }
  inline int statement_position() { return statement_position_; }
  inline Address pc() { return reloc_iterator_->rinfo()->pc(); }
  inline Code* code() { return debug_info_->code(); }
  inline RelocInfo* rinfo() { return reloc_iterator_->rinfo(); }
  inline RelocInfo::Mode rmode() const {
    return reloc_iterator_->rinfo()->rmode();
  }
  inline RelocInfo* original_rinfo() {
    return reloc_iterator_original_->rinfo();
  }
  inline RelocInfo::Mode original_rmode() const {
    return reloc_iterator_original_->rinfo()->rmode();
  }

  bool IsDebuggerStatement();

 protected:
  bool RinfoDone() const;
  void RinfoNext();

  BreakLocatorType type_;
  int break_point_;
  int position_;
  int statement_position_;
  Handle<DebugInfo> debug_info_;
  RelocIterator* reloc_iterator_;
  RelocIterator* reloc_iterator_original_;

 private:
  void SetDebugBreak();
  void ClearDebugBreak();

  void SetDebugBreakAtIC();
  void ClearDebugBreakAtIC();

  bool IsDebugBreakAtReturn();
  void SetDebugBreakAtReturn();
  void ClearDebugBreakAtReturn();

  bool IsDebugBreakSlot();
  bool IsDebugBreakAtSlot();
  void SetDebugBreakAtSlot();
  void ClearDebugBreakAtSlot();

  DISALLOW_COPY_AND_ASSIGN(BreakLocationIterator);
};


// Cache of all script objects in the heap. When a script is added a weak handle
// to it is created and that weak handle is stored in the cache. The weak handle
// callback takes care of removing the script from the cache. The key used in
// the cache is the script id.
class ScriptCache : private HashMap {
 public:
  explicit ScriptCache(Isolate* isolate)
      : HashMap(HashMap::PointersMatch),
        isolate_(isolate),
        collected_scripts_(10) {}
  virtual ~ScriptCache() { Clear(); }

  // Add script to the cache.
  void Add(Handle<Script> script);

  // Return the scripts in the cache.
  Handle<FixedArray> GetScripts();

  // Generate debugger events for collected scripts.
  void ProcessCollectedScripts();

 private:
  // Calculate the hash value from the key (script id).
  static uint32_t Hash(int key) {
    return ComputeIntegerHash(key, v8::internal::kZeroHashSeed);
  }

  // Clear the cache releasing all the weak handles.
  void Clear();

  // Weak handle callback for scripts in the cache.
  static void HandleWeakScript(
      const v8::WeakCallbackData<v8::Value, void>& data);

  Isolate* isolate_;
  // List used during GC to temporarily store id's of collected scripts.
  List<int> collected_scripts_;
};


// Linked list holding debug info objects. The debug info objects are kept as
// weak handles to avoid a debug info object to keep a function alive.
class DebugInfoListNode {
 public:
  explicit DebugInfoListNode(DebugInfo* debug_info);
  virtual ~DebugInfoListNode();

  DebugInfoListNode* next() { return next_; }
  void set_next(DebugInfoListNode* next) { next_ = next; }
  Handle<DebugInfo> debug_info() { return debug_info_; }

 private:
  // Global (weak) handle to the debug info object.
  Handle<DebugInfo> debug_info_;

  // Next pointer for linked list.
  DebugInfoListNode* next_;
};

// This class contains the debugger support. The main purpose is to handle
// setting break points in the code.
//
// This class controls the debug info for all functions which currently have
// active breakpoints in them. This debug info is held in the heap root object
// debug_info which is a FixedArray. Each entry in this list is of class
// DebugInfo.
class Debug {
 public:
  bool Load();
  void Unload();
  bool IsLoaded() { return !debug_context_.is_null(); }
  bool InDebugger() { return thread_local_.debugger_entry_ != NULL; }

  Object* Break(Arguments args);
  bool SetBreakPoint(Handle<JSFunction> function,
                     Handle<Object> break_point_object,
                     int* source_position);
  bool SetBreakPointForScript(Handle<Script> script,
                              Handle<Object> break_point_object,
                              int* source_position,
                              BreakPositionAlignment alignment);
  void ClearBreakPoint(Handle<Object> break_point_object);
  void ClearAllBreakPoints();
  void FloodWithOneShot(Handle<JSFunction> function);
  void FloodBoundFunctionWithOneShot(Handle<JSFunction> function);
  void FloodHandlerWithOneShot();
  void ChangeBreakOnException(ExceptionBreakType type, bool enable);
  bool IsBreakOnException(ExceptionBreakType type);

  void PromiseHandlePrologue(Handle<JSFunction> promise_getter);
  void PromiseHandleEpilogue();
  // Returns a promise if it does not have a reject handler.
  Handle<Object> GetPromiseForUncaughtException();

  void PrepareStep(StepAction step_action,
                   int step_count,
                   StackFrame::Id frame_id);
  void ClearStepping();
  void ClearStepOut();
  bool IsStepping() { return thread_local_.step_count_ > 0; }
  bool StepNextContinue(BreakLocationIterator* break_location_iterator,
                        JavaScriptFrame* frame);
  static Handle<DebugInfo> GetDebugInfo(Handle<SharedFunctionInfo> shared);
  static bool HasDebugInfo(Handle<SharedFunctionInfo> shared);

  void PrepareForBreakPoints();

  // This function is used in FunctionNameUsing* tests.
  Object* FindSharedFunctionInfoInScript(Handle<Script> script, int position);

  // Returns whether the operation succeeded. Compilation can only be triggered
  // if a valid closure is passed as the second argument, otherwise the shared
  // function needs to be compiled already.
  bool EnsureDebugInfo(Handle<SharedFunctionInfo> shared,
                       Handle<JSFunction> function);

  // Returns true if the current stub call is patched to call the debugger.
  static bool IsDebugBreak(Address addr);
  // Returns true if the current return statement has been patched to be
  // a debugger breakpoint.
  static bool IsDebugBreakAtReturn(RelocInfo* rinfo);

  // Check whether a code stub with the specified major key is a possible break
  // point location.
  static bool IsSourceBreakStub(Code* code);
  static bool IsBreakStub(Code* code);

  // Find the builtin to use for invoking the debug break
  static Handle<Code> FindDebugBreak(Handle<Code> code, RelocInfo::Mode mode);

  static Handle<Object> GetSourceBreakLocations(
      Handle<SharedFunctionInfo> shared,
      BreakPositionAlignment position_aligment);

  // Getter for the debug_context.
  inline Handle<Context> debug_context() { return debug_context_; }

  // Check whether a global object is the debug global object.
  bool IsDebugGlobal(GlobalObject* global);

  // Check whether this frame is just about to return.
  bool IsBreakAtReturn(JavaScriptFrame* frame);

  // Fast check to see if any break points are active.
  inline bool has_break_points() { return has_break_points_; }

  void NewBreak(StackFrame::Id break_frame_id);
  void SetBreak(StackFrame::Id break_frame_id, int break_id);
  StackFrame::Id break_frame_id() {
    return thread_local_.break_frame_id_;
  }
  int break_id() { return thread_local_.break_id_; }

  bool StepInActive() { return thread_local_.step_into_fp_ != 0; }
  void HandleStepIn(Handle<JSFunction> function,
                    Handle<Object> holder,
                    Address fp,
                    bool is_constructor);
  Address step_in_fp() { return thread_local_.step_into_fp_; }
  Address* step_in_fp_addr() { return &thread_local_.step_into_fp_; }

  bool StepOutActive() { return thread_local_.step_out_fp_ != 0; }
  Address step_out_fp() { return thread_local_.step_out_fp_; }

  EnterDebugger* debugger_entry() {
    return thread_local_.debugger_entry_;
  }
  void set_debugger_entry(EnterDebugger* entry) {
    thread_local_.debugger_entry_ = entry;
  }

  // Check whether any of the specified interrupts are pending.
  bool has_pending_interrupt() {
    return thread_local_.has_pending_interrupt_;
  }

  // Set specified interrupts as pending.
  void set_has_pending_interrupt(bool value) {
    thread_local_.has_pending_interrupt_ = value;
  }

  // Getter and setter for the disable break state.
  bool disable_break() { return disable_break_; }
  void set_disable_break(bool disable_break) {
    disable_break_ = disable_break;
  }

  // Getters for the current exception break state.
  bool break_on_exception() { return break_on_exception_; }
  bool break_on_uncaught_exception() {
    return break_on_uncaught_exception_;
  }

  enum AddressId {
    k_after_break_target_address,
    k_restarter_frame_function_pointer
  };

  // Support for setting the address to jump to when returning from break point.
  Address* after_break_target_address() {
    return reinterpret_cast<Address*>(&thread_local_.after_break_target_);
  }
  Address* restarter_frame_function_pointer_address() {
    Object*** address = &thread_local_.restarter_frame_function_pointer_;
    return reinterpret_cast<Address*>(address);
  }

  // Support for saving/restoring registers when handling debug break calls.
  Object** register_address(int r) {
    return &registers_[r];
  }

  static const int kEstimatedNofDebugInfoEntries = 16;
  static const int kEstimatedNofBreakPointsInFunction = 16;

  // Passed to MakeWeak.
  static void HandleWeakDebugInfo(
      const v8::WeakCallbackData<v8::Value, void>& data);

  friend class Debugger;
  friend Handle<FixedArray> GetDebuggedFunctions();  // In test-debug.cc
  friend void CheckDebuggerUnloaded(bool check_functions);  // In test-debug.cc

  // Threading support.
  char* ArchiveDebug(char* to);
  char* RestoreDebug(char* from);
  static int ArchiveSpacePerThread();
  void FreeThreadResources() { }

  // Mirror cache handling.
  void ClearMirrorCache();

  // Script cache handling.
  void CreateScriptCache();
  void DestroyScriptCache();
  void AddScriptToScriptCache(Handle<Script> script);
  Handle<FixedArray> GetLoadedScripts();

  // Record function from which eval was called.
  static void RecordEvalCaller(Handle<Script> script);

  // Garbage collection notifications.
  void AfterGarbageCollection();

  // Code generator routines.
  static void GenerateSlot(MacroAssembler* masm);
  static void GenerateCallICStubDebugBreak(MacroAssembler* masm);
  static void GenerateLoadICDebugBreak(MacroAssembler* masm);
  static void GenerateStoreICDebugBreak(MacroAssembler* masm);
  static void GenerateKeyedLoadICDebugBreak(MacroAssembler* masm);
  static void GenerateKeyedStoreICDebugBreak(MacroAssembler* masm);
  static void GenerateCompareNilICDebugBreak(MacroAssembler* masm);
  static void GenerateReturnDebugBreak(MacroAssembler* masm);
  static void GenerateCallFunctionStubDebugBreak(MacroAssembler* masm);
  static void GenerateCallConstructStubDebugBreak(MacroAssembler* masm);
  static void GenerateCallConstructStubRecordDebugBreak(MacroAssembler* masm);
  static void GenerateSlotDebugBreak(MacroAssembler* masm);
  static void GeneratePlainReturnLiveEdit(MacroAssembler* masm);

  // FrameDropper is a code replacement for a JavaScript frame with possibly
  // several frames above.
  // There is no calling conventions here, because it never actually gets
  // called, it only gets returned to.
  static void GenerateFrameDropperLiveEdit(MacroAssembler* masm);

  // Describes how exactly a frame has been dropped from stack.
  enum FrameDropMode {
    // No frame has been dropped.
    FRAMES_UNTOUCHED,
    // The top JS frame had been calling IC stub. IC stub mustn't be called now.
    FRAME_DROPPED_IN_IC_CALL,
    // The top JS frame had been calling debug break slot stub. Patch the
    // address this stub jumps to in the end.
    FRAME_DROPPED_IN_DEBUG_SLOT_CALL,
    // The top JS frame had been calling some C++ function. The return address
    // gets patched automatically.
    FRAME_DROPPED_IN_DIRECT_CALL,
    FRAME_DROPPED_IN_RETURN_CALL,
    CURRENTLY_SET_MODE
  };

  void FramesHaveBeenDropped(StackFrame::Id new_break_frame_id,
                                    FrameDropMode mode,
                                    Object** restarter_frame_function_pointer);

  // Initializes an artificial stack frame. The data it contains is used for:
  //  a. successful work of frame dropper code which eventually gets control,
  //  b. being compatible with regular stack structure for various stack
  //     iterators.
  // Returns address of stack allocated pointer to restarted function,
  // the value that is called 'restarter_frame_function_pointer'. The value
  // at this address (possibly updated by GC) may be used later when preparing
  // 'step in' operation.
  static Object** SetUpFrameDropperFrame(StackFrame* bottom_js_frame,
                                         Handle<Code> code);

  static const int kFrameDropperFrameSize;

  // Architecture-specific constant.
  static const bool kFrameDropperSupported;

  /**
   * Defines layout of a stack frame that supports padding. This is a regular
   * internal frame that has a flexible stack structure. LiveEdit can shift
   * its lower part up the stack, taking up the 'padding' space when additional
   * stack memory is required.
   * Such frame is expected immediately above the topmost JavaScript frame.
   *
   * Stack Layout:
   *   --- Top
   *   LiveEdit routine frames
   *   ---
   *   C frames of debug handler
   *   ---
   *   ...
   *   ---
   *      An internal frame that has n padding words:
   *      - any number of words as needed by code -- upper part of frame
   *      - padding size: a Smi storing n -- current size of padding
   *      - padding: n words filled with kPaddingValue in form of Smi
   *      - 3 context/type words of a regular InternalFrame
   *      - fp
   *   ---
   *      Topmost JavaScript frame
   *   ---
   *   ...
   *   --- Bottom
   */
  class FramePaddingLayout : public AllStatic {
   public:
    // Architecture-specific constant.
    static const bool kIsSupported;

    // A size of frame base including fp. Padding words starts right above
    // the base.
    static const int kFrameBaseSize = 4;

    // A number of words that should be reserved on stack for the LiveEdit use.
    // Normally equals 1. Stored on stack in form of Smi.
    static const int kInitialSize;
    // A value that padding words are filled with (in form of Smi). Going
    // bottom-top, the first word not having this value is a counter word.
    static const int kPaddingValue;
  };

 private:
  explicit Debug(Isolate* isolate);
  ~Debug();

  static bool CompileDebuggerScript(Isolate* isolate, int index);
  void ClearOneShot();
  void ActivateStepIn(StackFrame* frame);
  void ClearStepIn();
  void ActivateStepOut(StackFrame* frame);
  void ClearStepNext();
  // Returns whether the compile succeeded.
  void RemoveDebugInfo(Handle<DebugInfo> debug_info);
  void SetAfterBreakTarget(JavaScriptFrame* frame);
  Handle<Object> CheckBreakPoints(Handle<Object> break_point);
  bool CheckBreakPoint(Handle<Object> break_point_object);

  void EnsureFunctionHasDebugBreakSlots(Handle<JSFunction> function);
  void RecompileAndRelocateSuspendedGenerators(
      const List<Handle<JSGeneratorObject> > &suspended_generators);

  // Global handle to debug context where all the debugger JavaScript code is
  // loaded.
  Handle<Context> debug_context_;

  // Boolean state indicating whether any break points are set.
  bool has_break_points_;

  // Cache of all scripts in the heap.
  ScriptCache* script_cache_;

  // List of active debug info objects.
  DebugInfoListNode* debug_info_list_;

  bool disable_break_;
  bool break_on_exception_;
  bool break_on_uncaught_exception_;

  // When a promise is being resolved, we may want to trigger a debug event for
  // the case we catch a throw.  For this purpose we remember the try-catch
  // handler address that would catch the exception.  We also hold onto a
  // closure that returns a promise if the exception is considered uncaught.
  // Due to the possibility of reentry we use a list to form a stack.
  List<StackHandler*> promise_catch_handlers_;
  List<Handle<JSFunction> > promise_getters_;

  // Per-thread data.
  class ThreadLocal {
   public:
    // Counter for generating next break id.
    int break_count_;

    // Current break id.
    int break_id_;

    // Frame id for the frame of the current break.
    StackFrame::Id break_frame_id_;

    // Step action for last step performed.
    StepAction last_step_action_;

    // Source statement position from last step next action.
    int last_statement_position_;

    // Number of steps left to perform before debug event.
    int step_count_;

    // Frame pointer from last step next action.
    Address last_fp_;

    // Number of queued steps left to perform before debug event.
    int queued_step_count_;

    // Frame pointer for frame from which step in was performed.
    Address step_into_fp_;

    // Frame pointer for the frame where debugger should be called when current
    // step out action is completed.
    Address step_out_fp_;

    // Storage location for jump when exiting debug break calls.
    Address after_break_target_;

    // Stores the way how LiveEdit has patched the stack. It is used when
    // debugger returns control back to user script.
    FrameDropMode frame_drop_mode_;

    // Top debugger entry.
    EnterDebugger* debugger_entry_;

    // Pending interrupts scheduled while debugging.
    bool has_pending_interrupt_;

    // When restarter frame is on stack, stores the address
    // of the pointer to function being restarted. Otherwise (most of the time)
    // stores NULL. This pointer is used with 'step in' implementation.
    Object** restarter_frame_function_pointer_;
  };

  // Storage location for registers when handling debug break calls
  JSCallerSavedBuffer registers_;
  ThreadLocal thread_local_;
  void ThreadInit();

  Isolate* isolate_;

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(Debug);
};


DECLARE_RUNTIME_FUNCTION(Debug_Break);


// Message delivered to the message handler callback. This is either a debugger
// event or the response to a command.
class MessageImpl: public v8::Debug::Message {
 public:
  // Create a message object for a debug event.
  static MessageImpl NewEvent(DebugEvent event,
                              bool running,
                              Handle<JSObject> exec_state,
                              Handle<JSObject> event_data);

  // Create a message object for the response to a debug command.
  static MessageImpl NewResponse(DebugEvent event,
                                 bool running,
                                 Handle<JSObject> exec_state,
                                 Handle<JSObject> event_data,
                                 Handle<String> response_json,
                                 v8::Debug::ClientData* client_data);

  // Implementation of interface v8::Debug::Message.
  virtual bool IsEvent() const;
  virtual bool IsResponse() const;
  virtual DebugEvent GetEvent() const;
  virtual bool WillStartRunning() const;
  virtual v8::Handle<v8::Object> GetExecutionState() const;
  virtual v8::Handle<v8::Object> GetEventData() const;
  virtual v8::Handle<v8::String> GetJSON() const;
  virtual v8::Handle<v8::Context> GetEventContext() const;
  virtual v8::Debug::ClientData* GetClientData() const;
  virtual v8::Isolate* GetIsolate() const;

 private:
  MessageImpl(bool is_event,
              DebugEvent event,
              bool running,
              Handle<JSObject> exec_state,
              Handle<JSObject> event_data,
              Handle<String> response_json,
              v8::Debug::ClientData* client_data);

  bool is_event_;  // Does this message represent a debug event?
  DebugEvent event_;  // Debug event causing the break.
  bool running_;  // Will the VM start running after this event?
  Handle<JSObject> exec_state_;  // Current execution state.
  Handle<JSObject> event_data_;  // Data associated with the event.
  Handle<String> response_json_;  // Response JSON if message holds a response.
  v8::Debug::ClientData* client_data_;  // Client data passed with the request.
};


// Details of the debug event delivered to the debug event listener.
class EventDetailsImpl : public v8::Debug::EventDetails {
 public:
  EventDetailsImpl(DebugEvent event,
                   Handle<JSObject> exec_state,
                   Handle<JSObject> event_data,
                   Handle<Object> callback_data,
                   v8::Debug::ClientData* client_data);
  virtual DebugEvent GetEvent() const;
  virtual v8::Handle<v8::Object> GetExecutionState() const;
  virtual v8::Handle<v8::Object> GetEventData() const;
  virtual v8::Handle<v8::Context> GetEventContext() const;
  virtual v8::Handle<v8::Value> GetCallbackData() const;
  virtual v8::Debug::ClientData* GetClientData() const;
 private:
  DebugEvent event_;  // Debug event causing the break.
  Handle<JSObject> exec_state_;         // Current execution state.
  Handle<JSObject> event_data_;         // Data associated with the event.
  Handle<Object> callback_data_;        // User data passed with the callback
                                        // when it was registered.
  v8::Debug::ClientData* client_data_;  // Data passed to DebugBreakForCommand.
};


// Message send by user to v8 debugger or debugger output message.
// In addition to command text it may contain a pointer to some user data
// which are expected to be passed along with the command reponse to message
// handler.
class CommandMessage {
 public:
  static CommandMessage New(const Vector<uint16_t>& command,
                            v8::Debug::ClientData* data);
  CommandMessage();
  ~CommandMessage();

  // Deletes user data and disposes of the text.
  void Dispose();
  Vector<uint16_t> text() const { return text_; }
  v8::Debug::ClientData* client_data() const { return client_data_; }
 private:
  CommandMessage(const Vector<uint16_t>& text,
                 v8::Debug::ClientData* data);

  Vector<uint16_t> text_;
  v8::Debug::ClientData* client_data_;
};

// A Queue of CommandMessage objects.  A thread-safe version is
// LockingCommandMessageQueue, based on this class.
class CommandMessageQueue BASE_EMBEDDED {
 public:
  explicit CommandMessageQueue(int size);
  ~CommandMessageQueue();
  bool IsEmpty() const { return start_ == end_; }
  CommandMessage Get();
  void Put(const CommandMessage& message);
  void Clear() { start_ = end_ = 0; }  // Queue is empty after Clear().
 private:
  // Doubles the size of the message queue, and copies the messages.
  void Expand();

  CommandMessage* messages_;
  int start_;
  int end_;
  int size_;  // The size of the queue buffer.  Queue can hold size-1 messages.
};


class MessageDispatchHelperThread;


// LockingCommandMessageQueue is a thread-safe circular buffer of CommandMessage
// messages.  The message data is not managed by LockingCommandMessageQueue.
// Pointers to the data are passed in and out. Implemented by adding a
// Mutex to CommandMessageQueue.  Includes logging of all puts and gets.
class LockingCommandMessageQueue BASE_EMBEDDED {
 public:
  LockingCommandMessageQueue(Logger* logger, int size);
  bool IsEmpty() const;
  CommandMessage Get();
  void Put(const CommandMessage& message);
  void Clear();
 private:
  Logger* logger_;
  CommandMessageQueue queue_;
  mutable Mutex mutex_;
  DISALLOW_COPY_AND_ASSIGN(LockingCommandMessageQueue);
};


class Debugger {
 public:
  ~Debugger();

  void DebugRequest(const uint16_t* json_request, int length);

  MUST_USE_RESULT MaybeHandle<Object> MakeJSObject(
      Vector<const char> constructor_name,
      int argc,
      Handle<Object> argv[]);
  MUST_USE_RESULT MaybeHandle<Object> MakeExecutionState();
  MUST_USE_RESULT MaybeHandle<Object> MakeBreakEvent(
      Handle<Object> break_points_hit);
  MUST_USE_RESULT MaybeHandle<Object> MakeExceptionEvent(
      Handle<Object> exception,
      bool uncaught,
      Handle<Object> promise);
  MUST_USE_RESULT MaybeHandle<Object> MakeCompileEvent(
      Handle<Script> script, bool before);
  MUST_USE_RESULT MaybeHandle<Object> MakeScriptCollectedEvent(int id);

  void OnDebugBreak(Handle<Object> break_points_hit, bool auto_continue);
  void OnException(Handle<Object> exception, bool uncaught);
  void OnBeforeCompile(Handle<Script> script);

  enum AfterCompileFlags {
    NO_AFTER_COMPILE_FLAGS,
    SEND_WHEN_DEBUGGING
  };
  void OnAfterCompile(Handle<Script> script,
                      AfterCompileFlags after_compile_flags);
  void OnScriptCollected(int id);
  void ProcessDebugEvent(v8::DebugEvent event,
                         Handle<JSObject> event_data,
                         bool auto_continue);
  void NotifyMessageHandler(v8::DebugEvent event,
                            Handle<JSObject> exec_state,
                            Handle<JSObject> event_data,
                            bool auto_continue);
  void SetEventListener(Handle<Object> callback, Handle<Object> data);
  void SetMessageHandler(v8::Debug::MessageHandler handler);

  // Add a debugger command to the command queue.
  void EnqueueCommandMessage(Vector<const uint16_t> command,
                             v8::Debug::ClientData* client_data = NULL);

  // Check whether there are commands in the command queue.
  bool HasCommands();

  // Enqueue a debugger command to the command queue for event listeners.
  void EnqueueDebugCommand(v8::Debug::ClientData* client_data = NULL);

  MUST_USE_RESULT MaybeHandle<Object> Call(Handle<JSFunction> fun,
                                           Handle<Object> data);

  Handle<Context> GetDebugContext();

  // Unload the debugger if possible. Only called when no debugger is currently
  // active.
  void UnloadDebugger();
  friend void ForceUnloadDebugger();  // In test-debug.cc

  inline bool EventActive() {
    LockGuard<RecursiveMutex> lock_guard(&debugger_access_);

    // Check whether the message handler was been cleared.
    // TODO(yangguo): handle loading and unloading of the debugger differently.
    if (debugger_unload_pending_) {
      if (isolate_->debug()->debugger_entry() == NULL) {
        UnloadDebugger();
      }
    }

    // Currently argument event is not used.
    return !ignore_debugger_ && is_active_;
  }

  bool ignore_debugger() const { return ignore_debugger_; }
  void set_live_edit_enabled(bool v) { live_edit_enabled_ = v; }
  bool live_edit_enabled() const {
    return FLAG_enable_liveedit && live_edit_enabled_ ;
  }

  bool is_active() {
    LockGuard<RecursiveMutex> lock_guard(&debugger_access_);
    return is_active_;
  }

  class IgnoreScope {
   public:
    explicit IgnoreScope(Debugger* debugger)
        : debugger_(debugger),
          old_state_(debugger_->ignore_debugger_) {
      debugger_->ignore_debugger_ = true;
    }

    ~IgnoreScope() {
      debugger_->ignore_debugger_ = old_state_;
    }

   private:
    Debugger* debugger_;
    bool old_state_;
    DISALLOW_COPY_AND_ASSIGN(IgnoreScope);
  };

 private:
  explicit Debugger(Isolate* isolate);

  void CallEventCallback(v8::DebugEvent event,
                         Handle<Object> exec_state,
                         Handle<Object> event_data,
                         v8::Debug::ClientData* client_data);
  void CallCEventCallback(v8::DebugEvent event,
                          Handle<Object> exec_state,
                          Handle<Object> event_data,
                          v8::Debug::ClientData* client_data);
  void CallJSEventCallback(v8::DebugEvent event,
                           Handle<Object> exec_state,
                           Handle<Object> event_data);
  void ListenersChanged();

  // Invoke the message handler function.
  void InvokeMessageHandler(MessageImpl message);

  RecursiveMutex debugger_access_;  // Mutex guarding debugger variables.
  Handle<Object> event_listener_;  // Global handle to listener.
  Handle<Object> event_listener_data_;
  bool is_active_;
  bool ignore_debugger_;  // Are we temporarily ignoring the debugger?
  bool live_edit_enabled_;  // Enable LiveEdit.
  bool never_unload_debugger_;  // Can we unload the debugger?
  v8::Debug::MessageHandler message_handler_;
  bool debugger_unload_pending_;  // Was message handler cleared?

  static const int kQueueInitialSize = 4;
  LockingCommandMessageQueue command_queue_;
  Semaphore command_received_;  // Signaled for each command received.
  LockingCommandMessageQueue event_command_queue_;

  Isolate* isolate_;

  friend class EnterDebugger;
  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(Debugger);
};


// This class is used for entering the debugger. Create an instance in the stack
// to enter the debugger. This will set the current break state, make sure the
// debugger is loaded and switch to the debugger context. If the debugger for
// some reason could not be entered FailedToEnter will return true.
class EnterDebugger BASE_EMBEDDED {
 public:
  explicit EnterDebugger(Isolate* isolate);
  ~EnterDebugger();

  // Check whether the debugger could be entered.
  inline bool FailedToEnter() { return load_failed_; }

  // Check whether there are any JavaScript frames on the stack.
  inline bool HasJavaScriptFrames() { return has_js_frames_; }

  // Get the active context from before entering the debugger.
  inline Handle<Context> GetContext() { return save_.context(); }

 private:
  Isolate* isolate_;
  EnterDebugger* prev_;  // Previous debugger entry if entered recursively.
  JavaScriptFrameIterator it_;
  const bool has_js_frames_;  // Were there any JavaScript frames?
  StackFrame::Id break_frame_id_;  // Previous break frame id.
  int break_id_;  // Previous break id.
  bool load_failed_;  // Did the debugger fail to load?
  SaveContext save_;  // Saves previous context.
};


// Stack allocated class for disabling break.
class DisableBreak BASE_EMBEDDED {
 public:
  explicit DisableBreak(Isolate* isolate, bool disable_break)
    : isolate_(isolate) {
    prev_disable_break_ = isolate_->debug()->disable_break();
    isolate_->debug()->set_disable_break(disable_break);
  }
  ~DisableBreak() {
    isolate_->debug()->set_disable_break(prev_disable_break_);
  }

 private:
  Isolate* isolate_;
  // The previous state of the disable break used to restore the value when this
  // object is destructed.
  bool prev_disable_break_;
};


// Debug_Address encapsulates the Address pointers used in generating debug
// code.
class Debug_Address {
 public:
  explicit Debug_Address(Debug::AddressId id) : id_(id) { }

  static Debug_Address AfterBreakTarget() {
    return Debug_Address(Debug::k_after_break_target_address);
  }

  static Debug_Address RestarterFrameFunctionPointer() {
    return Debug_Address(Debug::k_restarter_frame_function_pointer);
  }

  Address address(Isolate* isolate) const {
    Debug* debug = isolate->debug();
    switch (id_) {
      case Debug::k_after_break_target_address:
        return reinterpret_cast<Address>(debug->after_break_target_address());
      case Debug::k_restarter_frame_function_pointer:
        return reinterpret_cast<Address>(
            debug->restarter_frame_function_pointer_address());
      default:
        UNREACHABLE();
        return NULL;
    }
  }

 private:
  Debug::AddressId id_;
};

} }  // namespace v8::internal

#endif  // V8_DEBUG_H_
