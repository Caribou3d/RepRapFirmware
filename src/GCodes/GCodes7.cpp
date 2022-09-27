/*
 * GCodes7.cpp
 *
 *  Created on: 14 Sept 2022
 *      Author: David
 *
 *  This file handles M291, M292 and other functions related to message boxes
 */

#include "GCodes.h"
#include "GCodeBuffer/GCodeBuffer.h"

// Process M291
GCodeResult GCodes::DoMessageBox(GCodeBuffer&gb, const StringRef& reply) THROWS(GCodeException)
{
	// Get the message
	gb.MustSee('P');
	String<MaxMessageLength> message;
	gb.GetQuotedString(message.GetRef());

	// Get the optional message box title
	bool dummy = false;
	String<StringLength100> title;
	(void)gb.TryGetQuotedString('R', title.GetRef(), dummy);

	// Get the message box mode
	uint32_t sParam = 1;
	(void)gb.TryGetLimitedUIValue('S', sParam, dummy, 8);

	// Get the optional timeout parameter. The default value depends on the mode (S parameter).
	float tParam = (sParam <= 1) ? DefaultMessageTimeout : 0.0;
	gb.TryGetNonNegativeFValue('T', tParam, dummy);

	bool displayCancelButton = false;
	gb.TryGetBValue('J', displayCancelButton, dummy);

	AxesBitmap axisControls;
	MessageBoxLimits limits;

	switch (sParam)
	{
	case 0:		// no buttons displayed, non-blocking
		if (tParam <= 0.0)
		{
			reply.copy("Attempt to create a message box that cannot be dismissed");
			return GCodeResult::error;
		}
		break;

	case 1:		// Close button displayed, non-blocking
	default:
		break;

	case 2:		// OK button displayed, blocking
	case 3:		// OK and Cancel buttons displayed, blocking
		// Message box modes 2 and 3 can take a list of axes that can be jogged
		for (size_t axis = 0; axis < numTotalAxes; axis++)
		{
			if (gb.Seen(axisLetters[axis]) && gb.GetIValue() > 0)
			{
				axisControls.SetBit(axis);
			}
		}
		break;

	case 4:		// Multiple choices, blocking
		gb.MustSee('K');
		limits.choices = gb.GetExpression();
		if (limits.choices.IsHeapStringArrayType())
		{
			uint32_t defaultChoice = 0;
			if (gb.TryGetUIValue('F', defaultChoice, dummy))
			{
				limits.defaultVal.SetInt((int32_t)defaultChoice);
			}
			break;
		}
		reply.copy("K parameter must be an array of strings");
		return GCodeResult::error;

	case 5:		// Integer value required, blocking
		limits.GetIntegerLimits(gb, false);
		break;

	case 6:		// Floating point value required, blocking
		limits.GetFloatLimits(gb);
		break;

	case 7:		// String value required, blocking
		limits.GetIntegerLimits(gb, true);
		break;
	}

	if (sParam >= 2)			// if it's a blocking message box
	{
		// Don't lock the movement system, because if we do then only the channel that issues the M291 can move the axes
#if HAS_SBC_INTERFACE
		if (reprap.UsingSbcInterface())
		{
			gb.SetState(GCodeState::waitingForAcknowledgement);
		}
#endif
		if (Push(gb, true))												// stack the machine state including the file position
		{
			UnlockMovement(gb);											// allow movement so that e.g. an SD card print can call M291 and then DWC or PanelDue can be used to jog axes
			gb.WaitForAcknowledgement();								// flag that we are waiting for acknowledgement
		}
	}

	// Display the message box on all relevant devices. Acknowledging any one of them clears them all.
	const MessageType mt = GetMessageBoxDevice(gb);						// get the display device
	reprap.SendAlert(mt, message.c_str(), title.c_str(), (int)sParam, tParam, axisControls, &limits);
	return GCodeResult::ok;
}

// Process M292
GCodeResult GCodes::AcknowledgeMessage(GCodeBuffer&gb, const StringRef& reply) THROWS(GCodeException)
{
	reprap.ClearAlert();

	const bool cancelled = (gb.Seen('P') && gb.GetIValue() == 1);
	for (GCodeBuffer* targetGb : gcodeSources)
	{
		if (targetGb != nullptr)
		{
			targetGb->MessageAcknowledged(cancelled);
		}
	}
	platform.MessageF(MessageType::LogInfo, "M292: cancelled: %s", (cancelled ? "true" : "false"));
	return GCodeResult::ok;
}

// End
