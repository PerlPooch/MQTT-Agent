#pragma once

static const char uiHtml[] PROGMEM = R"UIHTML(
<!doctype html>
<html>
<head>
	<meta charset="utf-8">
	<meta name="viewport" content="width=device-width,initial-scale=1">
	<title>MQTT Agent</title>
	<style>
		body {
			font-family: sans-serif;
			margin: 1rem;
			background: #111;
			color: #fff;
		}
		.card {
			background: #1b1b1b;
			border: 1px solid #333;
			border-radius: 8px;
			padding: 1rem;
			margin-bottom: 1rem;
		}
		h1, h2 {
			margin-top: 0;
			color: rgb(55, 152, 255);
		}
		.row {
			display: grid;
			grid-template-columns: 10rem 1fr auto;
			gap: 0.5rem;
			align-items: center;
			margin-bottom: 0.5rem;
		}
		.row.compact {
			grid-template-columns: 12rem 1fr;
		}
		input[type="text"],
		input[type="number"] {
			width: 100%;
			box-sizing: border-box;
			padding: 0.5rem;
			border: 1px solid #555;
			border-radius: 4px;
			background: #000;
			color: #eee;
		}
		button {
			padding: 0.5rem 0.75rem;
			border: 1px solid #555;
			border-radius: 4px;
			background: #2a2a2a;
			color: #eee;
			cursor: pointer;
		}
		button:hover {
			background: #333;
		}
		.relayButtons {
			display: flex;
			gap: 0.5rem;
			flex-wrap: wrap;
		}
		.page {
			max-width: 40rem;
			margin: 0;
		}
		.kv {
			display: grid;
			grid-template-columns: minmax(10rem, 12rem) minmax(0, 32rem);
			gap: 0.35rem 0.75rem;
			align-items: start;
		}
		.key {
			font-weight: 600;
		}
		.value {
			font-family: monospace;
			word-break: break-word;
		}
		pre {
			margin: 0;
			background: #000;
			padding: 0.75rem;
			border-radius: 6px;
			overflow: auto;
			border: 1px solid #333;
			max-height: 24rem;
		}
		#message {
			min-height: 1.2rem;
			margin-bottom: 1rem;
			color: #9f9;
		}
		.hidden {
			display: none;
		}
		a {
			color: #8cf;
		}
		.statusWithLamp {
			display: flex;
			align-items: center;
			gap: 0.5rem;
		}

		.lamp {
			width: 0.8rem;
			height: 0.8rem;
			border-radius: 50%;
			display: inline-block;
			border: 1px solid #666;
			background: #444;
			box-shadow: inset 0 0 2px rgba(0, 0, 0, 0.7);
			flex: 0 0 auto;
		}

		.lamp.on {
			background: #3fb950;
			box-shadow: 0 0 6px rgba(63, 185, 80, 0.8);
		}

		.lamp.off {
			background: #666;
		}
		.titleRow {
			display: flex;
			align-items: center;
			gap: 1rem;
			flex-wrap: wrap;
		}

		.updateIndicator {
			display: flex;
			align-items: center;
			gap: 0.5rem;
			font-size: 0.95rem;
			padding-bottom: 8px;
		}

		.lamp.busy {
			background: #d29922;
			box-shadow: 0 0 6px rgba(210, 153, 34, 0.8);
		}

		.lamp.error {
			background: #f85149;
			box-shadow: 0 0 6px rgba(248, 81, 73, 0.8);
		}
	</style>
</head>
<body>
	<div class="page">
		<div class="titleRow">
			<h1>MQTT Agent</h1>
		</div>
		<div class="updateIndicator">
			<span id="updateLamp" class="lamp off"></span>
			<span id="updateText">Idle</span>
		</div>
	
		<div class="card">
			<h2>Status</h2>
			<div id="statusSummary" class="kv"></div>
		</div>
	
		<div class="card">
			<h2>Configuration</h2>
	
			<div class="row">
				<label for="statusUpdateRate">Status Update Rate</label>
				<input id="statusUpdateRate" type="number" min="0" step="1">
				<button id="saveStatusRate">Save</button>
			</div>
	
			<div class="row">
				<label for="relayPulseDuration">Relay Pulse Duration</label>
				<input id="relayPulseDuration" type="number" min="0" step="1">
				<button id="savePulseDuration">Save</button>
			</div>
	
			<div class="row" id="tempRateRow">
				<label for="temperatureUpdateRate">Temperature Update Rate</label>
				<input id="temperatureUpdateRate" type="number" min="0" step="1">
				<button id="saveTempRate">Save</button>
			</div>
	
			<div class="row">
				<label for="broker">Broker</label>
				<input id="broker" type="text" placeholder="host or host:port">
				<button id="saveBroker">Save</button>
			</div>
		</div>
	
		<div class="card hidden" id="relayCard">
			<h2>Relays</h2>
	
			<div class="row compact hidden" id="relay0Row">
				<div>
					<span class="statusWithLamp">
						<strong>Relay 0</strong>
						<span id="relay0State"></span>
					</span>
				</div>
				<div class="relayButtons">
					<button data-relay="0" data-state="on">On</button>
					<button data-relay="0" data-state="off">Off</button>
					<button data-relay="0" data-state="pulse">Pulse</button>
				</div>
			</div>
	
			<div class="row compact hidden" id="relay1Row">
				<div>
					<span class="statusWithLamp">
						<strong>Relay 1</strong>
						<span id="relay1State"></span>
					</span>
				</div>
				<div class="relayButtons">
					<button data-relay="1" data-state="on">On</button>
					<button data-relay="1" data-state="off">Off</button>
					<button data-relay="1" data-state="pulse">Pulse</button>
				</div>
			</div>
		</div>
	
		<div class="card">
			<h2>Raw JSON</h2>
			<pre id="rawJson">{}</pre>
		</div>
	</div>

	<script>
		let updateLampTimer = null;

		function buildUrl(params) {
			const url = new URL("/", window.location.origin);
			if (params) {
				for (const [key, value] of Object.entries(params)) {
					url.searchParams.set(key, value);
				}
			}
			return url.toString();
		}

		function setUpdateLamp(state, text) {
			const lamp = document.getElementById("updateLamp");
			const label = document.getElementById("updateText");

			lamp.classList.remove("on", "off", "busy", "error");
			lamp.classList.add(state);

			label.textContent = text || "";
		}

		async function fetchStatus(params) {
			const res = await fetch(buildUrl(params), {
				method: "GET",
				cache: "no-store"
			});

			if (!res.ok) {
				throw new Error("HTTP " + res.status);
			}

			return await res.json();
		}

		function setUpdateLampWithTimeout(state, text, timeoutMs) {
			setUpdateLamp(state, text);

			if (updateLampTimer) {
				clearTimeout(updateLampTimer);
				updateLampTimer = null;
			}

			if (timeoutMs > 0) {
				updateLampTimer = setTimeout(function() {
					setUpdateLamp("off", "Idle");
					updateLampTimer = null;
				}, timeoutMs);
			}
		}
		
		function formatUptime(value) {
			const totalSeconds = parseInt(value / 1000, 10);
		
			if (!Number.isFinite(totalSeconds) || totalSeconds < 0) {
				return String(value);
			}
		
			const units = [
				{ label: "month", seconds: 30 * 24 * 60 * 60 },
				{ label: "week",  seconds: 7 * 24 * 60 * 60 },
				{ label: "day",   seconds: 24 * 60 * 60 },
				{ label: "hour",  seconds: 60 * 60 },
				{ label: "minute", seconds: 60 },
				{ label: "second", seconds: 1 }
			];
		
			let remaining = totalSeconds;
			const parts = [];
		
			for (const unit of units) {
				const count = Math.floor(remaining / unit.seconds);
				if (count > 0) {
					parts.push(count + "&nbsp;" + unit.label + (count === 1 ? "" : "s"));
					remaining %= unit.seconds;
				}
				if (parts.length >= 3) {
					break;
				}
			}
		
			if (!parts.length) {
				parts.push("0 seconds");
			}
		
			const days = (totalSeconds / 86400).toFixed(2);
		
//			return totalSeconds + " seconds (" + parts.join(", ") + "; " + days + " days)";
			return parts.join(", ") + ".<br>" + days + " days";
		}

		function isTruthyStatus(value) {
			if (typeof value === "boolean") {
				return value;
			}

			if (typeof value === "number") {
				return value !== 0;
			}

			const s = String(value).trim().toLowerCase();
			return s === "1" || s === "true" || s === "on";
		}

		function isLampStatusKey(key) {
			return key === "status0" || key === "status1";
		}
		
		function addKv(parent, key, value) {
			const k = document.createElement("div");
			k.className = "key";
			k.textContent = key;

			const v = document.createElement("div");
			v.className = "value";

			if (key === "uptime") {
				v.innerHTML = formatUptime(value);
			} else if (key === "build") {
				v.innerHTML = value.replace(/T/, ' ');
			} else if (isLampStatusKey(key)) {
				const wrapper = document.createElement("div");
				wrapper.className = "statusWithLamp";

				const lamp = document.createElement("span");
				lamp.className = "lamp " + (isTruthyStatus(value) ? "on" : "off");

				const text = document.createElement("span");
				text.textContent = String(value);

				wrapper.appendChild(text);
				wrapper.appendChild(lamp);

				v.appendChild(wrapper);
			} else {
				v.textContent = String(value);
			}
			
			parent.appendChild(k);
			parent.appendChild(v);
		}

		function renderStatus(data) {
			document.getElementById("rawJson").textContent = JSON.stringify(data, null, 2);

			const summary = document.getElementById("statusSummary");
			summary.innerHTML = "";

			const preferredOrder = [
				"version",
				"build",
				"id",
				"ip",
				"rssi",
				"config",
				"status0",
				"status1",
				"relay0",
				"relay1",
				"temperature",
				"temperatureAverage",
				"humidity",
				"temperatureUpdateRate",
				"statusUpdateRate",
				"relayPulseDuration",
				"broker",
				"brokerPort",
				"uptime"
			];

			const seen = new Set();

			for (const key of preferredOrder) {
				if (Object.prototype.hasOwnProperty.call(data, key)) {
					addKv(summary, key, data[key]);
					seen.add(key);
				}
			}

			for (const [key, value] of Object.entries(data)) {
				if (!seen.has(key)) {
					addKv(summary, key, value);
				}
			}

			if (Object.prototype.hasOwnProperty.call(data, "statusUpdateRate")) {
				document.getElementById("statusUpdateRate").value = parseInt(data.statusUpdateRate, 10) || 0;
			}

			if (Object.prototype.hasOwnProperty.call(data, "relayPulseDuration")) {
				document.getElementById("relayPulseDuration").value = parseInt(data.relayPulseDuration, 10) || 0;
				document.getElementById("relayPulseDuration").classList.remove("hidden");
			} else {
				document.getElementById("relayPulseDuration").classList.add("hidden");
			}

			if (Object.prototype.hasOwnProperty.call(data, "temperatureUpdateRate")) {
				document.getElementById("temperatureUpdateRate").value = parseInt(data.temperatureUpdateRate, 10) || 0;
				document.getElementById("tempRateRow").classList.remove("hidden");
			} else {
				document.getElementById("tempRateRow").classList.add("hidden");
			}

			if (Object.prototype.hasOwnProperty.call(data, "broker")) {
				let broker = data.broker || "";
				if (data.brokerPort !== undefined && String(data.brokerPort).length > 0 && broker.length > 0) {
					broker += ":" + data.brokerPort;
				}
				document.getElementById("broker").value = broker;
			}

			const relayCard = document.getElementById("relayCard");
			let anyRelay = false;

			if (Object.prototype.hasOwnProperty.call(data, "relay0")) {
				anyRelay = true;
				document.getElementById("relay0Row").classList.remove("hidden");

				const lamp = document.createElement("span");
				lamp.className = "lamp " + (isTruthyStatus(data.relay0) ? "on" : "off");

				const st = document.getElementById("relay0State");
				while (st.firstChild) {
					st.removeChild(st.firstChild);
				}
				st.appendChild(lamp);
			} else {
				document.getElementById("relay0Row").classList.add("hidden");
			}

			if (Object.prototype.hasOwnProperty.call(data, "relay1")) {
				anyRelay = true;
				document.getElementById("relay1Row").classList.remove("hidden");

				const lamp = document.createElement("span");
				lamp.className = "lamp " + (isTruthyStatus(data.relay1) ? "on" : "off");

				const st = document.getElementById("relay1State");
				while (st.firstChild) {
					st.removeChild(st.firstChild);
				}
				st.appendChild(lamp);
			} else {
				document.getElementById("relay1Row").classList.add("hidden");
			}

			relayCard.classList.toggle("hidden", !anyRelay);
		}

		async function refresh() {
			setUpdateLamp("busy", "Updating ...");

			try {
				const data = await fetchStatus();
				renderStatus(data);
				setUpdateLampWithTimeout("on", "Updated.", 1500);
			} catch (err) {
				setUpdateLamp("error", "Update failed: " + err.message);
			}
		}

		async function saveStatusRate() {
			const value = document.getElementById("statusUpdateRate").value;
			setUpdateLamp("busy", "Saving status update rate ...");

			try {
				const data = await fetchStatus({ statusUpdateRate: value });
				renderStatus(data);
				setUpdateLampWithTimeout("on", "Updated.", 1500);
			} catch (err) {
				setUpdateLamp("error", "Update failed: " + err.message);
			}
		}

		async function savePulseDuration() {
			const value = document.getElementById("relayPulseDuration").value;
			setUpdateLamp("busy", "Saving relay pulse duration ...");

			try {
				const data = await fetchStatus({ relayPulseDuration: value });
				renderStatus(data);
				setUpdateLampWithTimeout("on", "Updated.", 1500);
			} catch (err) {
				setUpdateLamp("error", "Update failed: " + err.message);
			}
		}

		async function saveTempRate() {
			const value = document.getElementById("temperatureUpdateRate").value;
			setUpdateLamp("busy", "Saving temperature update rate ...");

			try {
				const data = await fetchStatus({ temperatureUpdateRate: value });
				renderStatus(data);
				setUpdateLampWithTimeout("on", "Updated.", 1500);
			} catch (err) {
				setUpdateLamp("error", "Update failed: " + err.message);
			}
		}

		async function saveBroker() {
			const value = document.getElementById("broker").value;
			setUpdateLamp("busy", "Saving MQTT broker ...");

			try {
				const data = await fetchStatus({ broker: value });
				renderStatus(data);
				setUpdateLampWithTimeout("on", "Updated.", 1500);
			} catch (err) {
				setUpdateLamp("error", "Update failed: " + err.message);
			}
		}

		async function relay(deviceNum, state) {
			setUpdateLamp("busy", "Setting relay " + deviceNum + " ...");

			try {
				const data = await fetchStatus({
					set: "1",
					"device-num": String(deviceNum),
					state: state
				});
				renderStatus(data);
				setUpdateLampWithTimeout("on", "Updated Relay " + deviceNum + " " + state + ".", 1500);
			} catch (err) {
				setUpdateLamp("error", "Update failed: " + err.message);
			}
		}


		document.getElementById("saveStatusRate").addEventListener("click", saveStatusRate);
		document.getElementById("savePulseDuration").addEventListener("click", savePulseDuration);
		document.getElementById("saveTempRate").addEventListener("click", saveTempRate);
		document.getElementById("saveBroker").addEventListener("click", saveBroker);

		document.querySelectorAll("[data-relay]").forEach(function(btn) {
			btn.addEventListener("click", function() {
				relay(btn.dataset.relay, btn.dataset.state);
			});
		});

		refresh();
		setInterval(refresh, 5000);
	</script>
</body>
</html>
)UIHTML";

