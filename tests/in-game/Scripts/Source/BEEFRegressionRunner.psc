Scriptname BEEFRegressionRunner extends Quest

Int Property command = 0 Auto
Int request = 0
Int phase = 0
Int polls = 0
Bool ownsArmor = False
Bool awaitingVisual = False
Bool allVisualsPassed = True
String session = ""
String lastResult = "IDLE"
Armor fixture
Actor target

Event OnInit()
    Tell("Ready - no run active", "open the console and enter setpqv beef_regression command 1 to start")
    RegisterForSingleUpdate(0.5)
EndEvent

Function Tell(String status, String todo)
    Debug.Trace("BEEF regression: " + status + ". Do: " + todo)
    Debug.Notification("BEEF regression: " + status)
    Debug.Notification("Do: " + todo)
EndFunction

String Function StepLabel(Int step)
    If step == 1
        Return "Step 1/4 Equip"
    ElseIf step == 2
        Return "Step 2/4 Apply"
    ElseIf step == 3
        Return "Step 3/4 Retire"
    ElseIf step == 4
        Return "Step 4/4 Reapply"
    EndIf
    Return "No step"
EndFunction

String Function LookFor(Int step)
    If step == 2
        Return "check that Arcane Circuit is visible on the cuirass"
    ElseIf step == 3
        Return "check that the cuirass is plain dwarven armor and still equipped"
    ElseIf step == 4
        Return "check that Arcane Circuit is back on the cuirass"
    EndIf
    Return "check the cuirass"
EndFunction

String Function WaitingFor(Int step)
    If step == 1
        Return "waiting for the demo cuirass to equip"
    ElseIf step == 3
        Return "waiting for the plugin to remove the effects"
    EndIf
    Return "waiting for the plugin to apply the effects"
EndFunction

String Function Cause(String result, Int step)
    If result == "BLOCKED" && step == 3
        Return "the player or the plugin was not ready"
    ElseIf result == "BLOCKED"
        Return "no Arcane Circuit output rendered, or the player was not ready"
    ElseIf result == "FAIL" && step == 3
        Return "effects were still live after the retire"
    ElseIf result == "FAIL"
        Return "the plugin failed to apply the effects"
    EndIf
    Return "the plugin cancelled the request"
EndFunction

Function TellWaiting(String note = "")
    Tell(note + StepLabel(phase) + " - " + WaitingFor(phase), "keep the console closed and wait")
EndFunction

Function TellCheckpoint()
    Tell(StepLabel(phase) + " - plugin check passed", LookFor(phase) + ", then enter command 2 if yes or 3 if no")
EndFunction

Bool Function CurrentSession()
    Return session == BEEFRegressionNative.Session()
EndFunction

Function Run(String caseName = "lifecycle")
    If phase != 0
        Tell("Not started - a run is already active", "enter command 4 for its status or 5 to abort it")
        Return
    EndIf
    If caseName != "lifecycle"
        Tell("Not started - unknown case " + caseName, "enter command 1 to run the lifecycle case")
        Return
    EndIf
    target = Game.GetPlayer()
    fixture = Game.GetFormFromFile(0x803, "BetterEnchantmentEffectsDemo.esp") as Armor
    If !target || !fixture
        Tell("Not started - the demo plugin is not loaded", "enable BetterEnchantmentEffectsDemo.esp, restart, then enter command 1")
        Return
    EndIf
    If target.GetWornForm(4) || target.GetItemCount(fixture) > 0
        Tell("Not started - body armor is worn or the demo cuirass is carried", "unequip body armor, drop the demo cuirass, then enter command 1")
        Return
    EndIf
    session = BEEFRegressionNative.Session()
    If session == ""
        Tell("Not started - the plugin bridge is unavailable", "install the current plugin DLL, restart, then enter command 1")
        Return
    EndIf
    request = 0
    BEEFRegressionNative.Solo("arcane-circuit")
    target.AddItem(fixture, 1, True)
    ownsArmor = True
    target.EquipItem(fixture, False, True)
    phase = 1
    polls = 0
    allVisualsPassed = True
    lastResult = "WAITING FOR EQUIP"
    Tell(StepLabel(phase) + " - Arcane Circuit soloed, demo cuirass added", "close the console and wait")
    RegisterForSingleUpdate(0.1)
EndFunction

Function Submit(Bool retire, String note = "")
    request = BEEFRegressionNative.Submit(target, retire)
    polls = 0
    awaitingVisual = False
    If request == 0
        Finish("Run BLOCKED at " + StepLabel(phase) + " - the plugin bridge is busy", "wait a moment, then enter command 1")
        Return
    EndIf
    lastResult = "WAITING"
    TellWaiting(note)
    RegisterForSingleUpdate(0.1)
EndFunction

Event OnUpdate()
    If Utility.IsInMenuMode()
        RegisterForSingleUpdate(0.1)
        Return
    EndIf
    If phase != 0 && !CurrentSession()
        BEEFRegressionNative.RestoreView()
        phase = 0
        ownsArmor = False
        awaitingVisual = False
        command = 0
        lastResult = "ABORTED: the game was saved or loaded"
        Tell("Run ABORTED - the game was saved or loaded during the run", "remove any demo cuirass by hand, then enter command 1")
    EndIf
    Int action = command
    command = 0
    If action == 1
        Run()
    ElseIf action == 2
        Verdict(True)
    ElseIf action == 3
        Verdict(False)
    ElseIf action == 4
        Status()
    ElseIf action == 5
        Abort()
    ElseIf action != 0
        Tell("Not accepted - unknown command " + action, "enter 1 start, 2 yes, 3 no, 4 status or 5 abort")
    EndIf
    If phase == 0
        RegisterForSingleUpdate(0.5)
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
        Finish("Run FAIL at " + StepLabel(phase) + " - timed out " + WaitingFor(phase), "check the Papyrus and plugin logs, then enter command 1")
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
            TellCheckpoint()
        ElseIf lastResult != "WAITING"
            Finish("Run " + lastResult + " at " + StepLabel(phase) + " - " + Cause(lastResult, phase), "check the plugin log, then enter command 1")
            Return
        EndIf
    EndIf
    RegisterForSingleUpdate(0.1)
EndEvent

Function Verdict(Bool passed)
    If !awaitingVisual || !CurrentSession()
        Tell("Not accepted - no step is waiting for your answer", "enter command 4 for the status")
        Return
    EndIf
    allVisualsPassed = allVisualsPassed && passed
    BEEFRegressionNative.Mark(request, "lifecycle." + phase, passed)
    String answer = "no"
    If passed
        answer = "yes"
    EndIf
    UnregisterForUpdate()
    If phase == 4
        If allVisualsPassed
            Finish("Run PASS - every plugin check passed and you answered yes each time", "nothing; enter command 1 to run again")
        Else
            Finish("Run FAIL - you answered no at least once", "note which step looked wrong, then enter command 1 to run again")
        EndIf
        Return
    EndIf
    phase += 1
    Submit(phase == 3, "You answered " + answer + ". ")
EndFunction

Function Status()
    Debug.Trace("BEEF regression: phase=" + phase + " request=" + request + " result=" + lastResult + " visual=" + awaitingVisual)
    If phase == 0
        Tell("No run active - last result: " + lastResult, "enter command 1 to start a run")
    ElseIf awaitingVisual
        TellCheckpoint()
    Else
        TellWaiting()
    EndIf
EndFunction

Function Abort()
    If phase == 0
        Tell("Not accepted - no run is active", "enter command 1 to start a run")
        Return
    EndIf
    If CurrentSession() && request > 0
        BEEFRegressionNative.Abort(request)
    EndIf
    Finish("Run ABORTED by command 5", "enter command 1 to run again")
EndFunction

Function Finish(String result, String todo)
    UnregisterForUpdate()
    String cleanup = "solo restored"
    If ownsArmor && CurrentSession() && target && fixture
        target.UnequipItem(fixture, False, True)
        target.RemoveItem(fixture, 1, True)
        cleanup = "cuirass removed, solo restored"
    EndIf
    BEEFRegressionNative.RestoreView()
    ownsArmor = False
    awaitingVisual = False
    phase = 0
    lastResult = result
    Tell(result + "; " + cleanup, todo)
    RegisterForSingleUpdate(0.5)
EndFunction
