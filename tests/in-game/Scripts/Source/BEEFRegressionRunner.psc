Scriptname BEEFRegressionRunner extends Quest

Int request = 0
Int phase = 0
Int polls = 0
Bool ownsArmor = False
Bool awaitingVisual = False
Bool verdictRecorded = False
Bool visualPassed = False
Bool allVisualsPassed = True
String session = ""
String lastResult = "IDLE"
Armor fixture
Actor target

Function Say(String message)
    Debug.Trace("BEEF regression: " + message)
    Debug.Notification("BEEF regression: " + message)
EndFunction

Bool Function CurrentSession()
    Return session == BEEFRegressionNative.Session()
EndFunction

Function Run(String caseName = "lifecycle")
    If phase != 0
        Say("A run is active; use Status or Abort")
        Return
    EndIf
    If caseName != "lifecycle"
        Say("BLOCKED: unknown case " + caseName)
        Return
    EndIf
    target = Game.GetPlayer()
    fixture = Game.GetFormFromFile(0x803, "BetterEnchantmentEffectsDemo.esp") as Armor
    If !target || !fixture
        Say("BLOCKED: missing player or demo plugin")
        Return
    EndIf
    If target.GetWornForm(4) || target.GetItemCount(fixture) > 0
        Say("BLOCKED: clear the body slot and remove existing demo armor first")
        Return
    EndIf
    session = BEEFRegressionNative.Session()
    If session == ""
        Say("BLOCKED: native bridge unavailable")
        Return
    EndIf
    request = 0
    target.AddItem(fixture, 1, True)
    ownsArmor = True
    target.EquipItem(fixture, False, True)
    phase = 1
    polls = 0
    allVisualsPassed = True
    lastResult = "WAITING FOR EQUIP"
    Say("Started lifecycle; close the console")
    RegisterForSingleUpdate(0.1)
EndFunction

Function Submit(Bool retire)
    request = BEEFRegressionNative.Submit(target, retire)
    polls = 0
    awaitingVisual = False
    verdictRecorded = False
    If request == 0
        Finish("BLOCKED: bridge unavailable or busy")
        Return
    EndIf
    lastResult = "WAITING"
    RegisterForSingleUpdate(0.1)
EndFunction

Event OnUpdate()
    If phase == 0
        Return
    EndIf
    If !CurrentSession()
        UnregisterForUpdate()
        phase = 0
        ownsArmor = False
        awaitingVisual = False
        lastResult = "ABORTED: save/session changed; inspect saved equipment manually"
        Say(lastResult)
        Return
    EndIf
    If Utility.IsInMenuMode()
        RegisterForSingleUpdate(0.1)
        Return
    EndIf
    If awaitingVisual
        RegisterForSingleUpdate(0.5)
        Return
    EndIf
    polls += 1
    If polls > 100
        If request > 0
            BEEFRegressionNative.Abort(request)
        EndIf
        Finish("FAIL: timed out waiting for equip or plugin completion")
        Return
    EndIf
    If phase == 1
        If target.IsEquipped(fixture)
            phase = 2
            Submit(False)
            Return
        EndIf
    Else
        lastResult = BEEFRegressionNative.Result(request)
        If lastResult == "PASS"
            awaitingVisual = True
            Say("Machine PASS at checkpoint " + phase + "; inspect, RecordVisual, then Next")
        ElseIf lastResult != "WAITING"
            Finish(lastResult)
            Return
        EndIf
    EndIf
    RegisterForSingleUpdate(0.1)
EndEvent

Function RecordVisual(Bool passed)
    If !awaitingVisual || !CurrentSession()
        Say("No current visual checkpoint")
        Return
    EndIf
    visualPassed = passed
    verdictRecorded = True
    BEEFRegressionNative.Mark(request, "lifecycle." + phase, passed)
    Say("Visual verdict recorded: " + passed)
EndFunction

Function Next()
    If !awaitingVisual || !verdictRecorded || !CurrentSession()
        Say("Record a visual verdict at the current checkpoint first")
        Return
    EndIf
    allVisualsPassed = allVisualsPassed && visualPassed
    UnregisterForUpdate()
    If phase == 2
        phase = 3
        Submit(True)
    ElseIf phase == 3
        phase = 4
        Submit(False)
    ElseIf phase == 4
        If allVisualsPassed
            Finish("PASS: lifecycle assertions and visuals")
        Else
            Finish("FAIL: lifecycle visual verdict")
        EndIf
    EndIf
EndFunction

Function Status()
    Say("phase=" + phase + " request=" + request + " result=" + lastResult + " visual=" + awaitingVisual)
EndFunction

Function Abort()
    If CurrentSession() && request > 0
        BEEFRegressionNative.Abort(request)
    EndIf
    Finish("ABORTED")
EndFunction

Function Finish(String result)
    UnregisterForUpdate()
    If ownsArmor && CurrentSession() && target && fixture
        target.UnequipItem(fixture, False, True)
        target.RemoveItem(fixture, 1, True)
    EndIf
    ownsArmor = False
    awaitingVisual = False
    verdictRecorded = False
    phase = 0
    lastResult = result
    Say(result)
EndFunction
