# Spencer MQTT Agent

## MQTT Topic

	spencer/<ID>/<device>
		<ID> := unique MAC address of device
		<device> := temperature|status|relay
		
## Payload (JSON object):

	{
		"command": "fetch"|"set-rate"|"set"
		"rate": int: seconds between updates
		"device-num": int: 0-based index of device 
		"state": "on"|"off"|"pulse"
	}
	

	command == "fetch"
		Causes agent to publish information for <device>

	command == "set-rate"
		Sets update rate for <device> to rate in rate. <device> can be temperature or status

	command == "set"
		Sets <device> at index device-num to state. <device> can be relay. <state> can be on, off, or pulse.
		
## Examples:

	spencer/FC:F5:C4:AB:56:24/temperature {"command":"set-rate", "rate":"120"}
		Set the update rate for temperature reports to be 120 seconds

	spencer/FC:F5:C4:AB:56:24/status {"command":"set-rate", "rate":"300"}
		Set the update rate for status reports to be 300 seconds

	spencer/FC:F5:C4:AB:56:24/relay {"command":"set", "device-num":"1", "state":"on"}
		Turn on relay 0

	spencer/FC:F5:C4:AB:56:24/relay {"command":"set", "device-num":"1", "state":"off"}
		Turn off relay 1

	spencer/FC:F5:C4:AB:56:24/relay {"command":"set", "device-num":"0", "state":"pulse"}
		Pulse relay 0

	spencer/FC:F5:C4:AB:56:24/temperature {"command":"fetch"}
		Request publish of temperature

	spencer/FC:F5:C4:AB:56:24/status {"command":"fetch"}
		Request publish of input status

	spencer/FC:F5:C4:AB:56:24/relay {"command":"fetch"}
		Request publish of relay status

