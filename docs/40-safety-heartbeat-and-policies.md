# Safety, Heartbeat (Lease), and Disconnect Policies

## Objectives
- Prevent “start” unless the operator HMI is actively connected and live
- Allow a run to continue if the tablet disconnects mid-run (unless there are faults)
- Maintain hardware-first safety (E-stop and physical interlocks are authoritative)

## Session + Lease (“Heartbeat”) model

### 1) Open session
- App sends COMMAND: OPEN_SESSION
- ESP responds with COMMAND_ACK (Indicate recommended):
  - `session_id (u32)`
  - `lease_ms (u16)` e.g., 3000 ms

### 2) Keepalive
- App sends KEEPALIVE every 1 second:
  - `{ session_id }`
- ESP updates `last_seen_ms`

### 3) Lease expiry
If `now - last_seen_ms > lease_ms`:
- Mark HMI as not-live
- Raise warning event: HMI_DISCONNECTED (Notify)
- Log the event

## Start gating policy
START_RUN is accepted only if:
- HMI lease is valid
- No latched critical alarms
- Interlocks satisfied

If not, reject with:
- COMMAND_ACK status = REJECTED_POLICY

## Mid-run disconnect policy
If HMI lease expires mid-run:
- Do NOT stop the run by default
- Raise a warning event and log
- Optionally:
  - inhibit non-essential commands until reconnect
  - allow STOP/ABORT if user reconnects

This matches the requirement: “cannot start without client live,” but “run can continue if tablet disconnects.”

## Critical events and Indicate
Use Indicate for:
- E-stop asserted/cleared
- START/STOP/ABORT acknowledgements
- Latched CRITICAL alarms

Rationale: must-land semantics.

## UI expectations
- Prominent connection state indicator:
  - CONNECTING / LIVE / DEGRADED / DISCONNECTED
- Clear “HMI required to start” messaging
- Alarm banner that distinguishes WARNING vs CRITICAL

## Safety note
This heartbeat is a *policy gate*, not your primary safety function.
Primary safety remains physical:
- E-stop chain
- interlocks
- safe output defaults on boot and fault
