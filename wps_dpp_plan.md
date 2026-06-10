Now I have the complete picture. Let me map the exact existing WPS path and then show precisely where DPP PBC hooks in, reusing as much as possible.

---

**Existing WPS PBC path — complete end-to-end:**

```
TRIGGER
  ubus call X_PRPLWARE-COM_Agent.WPS InitiateWPSPBC
    ↓
  agent/src/beerocks/slave/nbapi/on_action.cpp
    initiate_wps_pbc callback (set in beerocks_agent_main.cpp:520)
    ↓
  BackhaulManager::initiate_wps_pbc_auto()       [backhaul_manager.cpp:397]
    ↓
  BackhaulManager::start_wps_pbc(radio_mac)      [backhaul_manager.cpp:367]
    ├─ if OPERATIONAL → start_wps_pbc_ap()       [AP/proxy path]
    └─ if not OPERATIONAL → start_wps_pbc_sta()  [bSTA/enrollee path]

AP PATH (proxy agent already in network):
  start_wps_pbc_ap()
    → create cACTION_BACKHAUL_START_WPS_PBC_REQUEST
    → send to son_slave_thread via m_agent_fd
    ↓
  son_slave_thread.cpp:1803
    ACTION_BACKHAUL_START_WPS_PBC_REQUEST
    → create cACTION_APMANAGER_START_WPS_PBC_REQUEST
    → send to ap_manager
    ↓
  ap_manager.cpp:2062
    ACTION_APMANAGER_START_WPS_PBC_REQUEST
    → ap_wlan_hal->start_wps_pbc()
    ↓
  bwl/whm/ap_wlan_hal_whm.cpp:1233
    start_wps_pbc()
    → m_ambiorix_cl.call(wps_path, "InitiateWPSPBC", args, result)
    ↓
  pwhm: Device.WiFi.AccessPoint.{i}.WPS.InitiateWPSPBC()
    → WPS_PBC   [hostapd via wpa_ctrl]

bSTA PATH (enrollee, not yet in network):
  start_wps_pbc_sta()
    → sta_wlan_hal->start_wps_pbc()
    ↓
  bwl/whm/sta_wlan_hal_whm.cpp:221
    start_wps_pbc()
    → pwhm: Device.WiFi.EndPoint.{i}.WPS.pushButton()
    → WPS_PBC multi_ap=1  [wpa_supplicant via wpa_ctrl]
    ↓
  BackhaulManager FSM → WAIT_WPS state [backhaul_manager.cpp:1233]

WPS EVENTS BACK:
  hostapd/wpa_supplicant emits WPS_EVENT_TIMEOUT / WPS_EVENT_FAIL / WPS_EVENT_CANCEL
    ↓
  pwhm receives via wpaCtrlEvents
    ↓
  ap_wlan_hal::Event::WPS_Event_Timeout / WPS_Event_Fail / WPS_Event_Cancel
    ↓
  ap_manager.cpp:3636 / backhaul_manager handles FSM transitions
```

---

**DPP PBC integration — reuse map:**

```
WHAT TO REUSE               WHAT TO ADD / MODIFY
─────────────────────────   ────────────────────────────────────────
initiate_wps_pbc_auto()  → extend: detect DPP mode, call DPP bootstrap first
start_wps_pbc_sta()      → extend: pass dpp_uri to sta_wlan_hal
sta_wlan_hal->start_wps_pbc() → extend signature: start_wps_pbc(dpp_uri="")
cACTION_BACKHAUL_START_WPS_PBC_REQUEST → reuse for AP path (no change needed)
cACTION_APMANAGER_START_WPS_PBC_REQUEST → reuse for AP path (no change needed)
ap_wlan_hal->start_wps_pbc() → extend: enable dpp_uri_no_cred before WPS_PBC
WPS_Event_Timeout/Fail/Cancel → reuse existing event handling
WAIT_WPS FSM state        → reuse as-is
```

---

**Precise integration points — what changes where:**

**1. BWL base interface — two new functions + extend existing:**

```cpp
// base_wlan_hal.h / sta_wlan_hal.h

// EXISTING — extend signature (default empty = plain WPS, backward compatible)
virtual bool start_wps_pbc(const std::string &dpp_uri = "") = 0;

// NEW — two DPP-specific functions
virtual int  dpp_bootstrap_gen(const std::string &params) = 0;
virtual std::string dpp_bootstrap_get_uri(int bootstrap_id) = 0;
```

WHM backend implementation:
```cpp
// sta_wlan_hal_whm.cpp — extend start_wps_pbc
bool sta_wlan_hal_whm::start_wps_pbc(const std::string &dpp_uri)
{
    // existing path: call pwhm endpoint WPS pushButton
    // NEW: if dpp_uri not empty, pass it as parameter to pwhm
    // pwhm formats: WPS_PBC multi_ap=1 dpp_uri=<uri>
    amxc_var_t args;
    if (!dpp_uri.empty()) {
        amxc_var_add_key(cstring_t, &args, "dpp_uri", dpp_uri.c_str());
    }
    return m_ambiorix_cl.call(m_ep_path + "WPS.", "pushButton", args, result);
}

// NEW: dpp_bootstrap_gen — calls pwhm DPP endpoint
int sta_wlan_hal_whm::dpp_bootstrap_gen(const std::string &params)
{
    // pwhm translates to: DPP_BOOTSTRAP_GEN type=qrcode ...
    // returns bootstrap ID as integer
    amxc_var_t args, result;
    m_ambiorix_cl.call(m_ep_path + "DPP.", "BootstrapGen", args, result);
    return amxc_var_dyncast(int32_t, &result);
}

// NEW: dpp_bootstrap_get_uri
std::string sta_wlan_hal_whm::dpp_bootstrap_get_uri(int bootstrap_id)
{
    // pwhm translates to: DPP_GET_URI <id>
    amxc_var_t args, result;
    amxc_var_add_key(int32_t, &args, "bootstrap_id", bootstrap_id);
    m_ambiorix_cl.call(m_ep_path + "DPP.", "GetURI", args, result);
    return std::string(amxc_var_constcast(cstring_t, &result));
}
```

---

**2. BackhaulManager — extend start_wps_pbc_sta() only:**

```cpp
// backhaul_manager.cpp — extend start_wps_pbc_sta()
bool BackhaulManager::start_wps_pbc_sta()
{
    auto sta_wlan_hal = get_selected_backhaul_sta_wlan_hal();
    if (!sta_wlan_hal) {
        LOG(ERROR) << "Failed to get backhaul STA hal";
        return false;
    }

    // NEW: DPP PBC path — generate URI before WPS_PBC
    std::string dpp_uri;
    if (m_dpp_pbc_enabled) {  // flag set when DPP mode requested
        auto db        = AgentDB::get();
        std::string params = "type=qrcode chan=" + get_backhaul_channel_str()
                           + " mac=" + tlvf::mac_to_string(db->bridge.mac);
        int bootstrap_id = sta_wlan_hal->dpp_bootstrap_gen(params);
        if (bootstrap_id >= 0) {
            dpp_uri = sta_wlan_hal->dpp_bootstrap_get_uri(bootstrap_id);
            LOG(INFO) << "DPP PBC: generated URI for bootstrap_id: " << bootstrap_id;
        } else {
            LOG(WARNING) << "DPP bootstrap_gen failed, falling back to plain WPS";
        }
    }

    // EXISTING call — now passes dpp_uri (empty = plain WPS, backward compatible)
    if (!sta_wlan_hal->start_wps_pbc(dpp_uri)) {
        LOG(ERROR) << "Failed to start wps";
        return false;
    }
    return true;
    // FSM already moves to WAIT_WPS — REUSED AS-IS
}
```

---

**3. ap_wlan_hal (proxy agent) — extend start_wps_pbc + new event:**

```cpp
// ap_wlan_hal_whm.cpp — extend start_wps_pbc
bool ap_wlan_hal_whm::start_wps_pbc()
{
    // NEW: if DPP mode and 1905 TK exists, enable no-cred first
    if (m_dpp_uri_no_cred_needed) {
        m_ambiorix_cl.set(wps_path + "dpp_uri_no_cred", true);
    }
    // EXISTING path unchanged
    bool ret = m_ambiorix_cl.call(wps_path, "InitiateWPSPBC", args, result);
    return ret;
}

// NEW event in ap_wlan_hal.h enum
enum Event {
    // ... existing events ...
    WPS_Event_Timeout,
    WPS_Event_Fail,
    WPS_Event_Cancel,
    WPS_DPP_URI_Event,    // NEW: WPS-DPP-URI received from hostapd
};

// NEW event data struct
struct sWpsDppUriParams {
    std::string uri;
    sMacAddr    sta_mac;
    std::string bssid;
};
```

pwhm already receives `WPS-DPP-URI` as a raw event via `fProcEvtMsg`. Add parsing:

```cpp
// ap_wlan_hal_whm.cpp — in event processing
if (event_str.find("WPS-DPP-URI") != std::string::npos) {
    // parse: WPS-DPP-URI uri=DPP:... bss=xx:xx sta=xx:xx
    sWpsDppUriParams params;
    // ... parse uri, bss, sta fields ...
    event_queue_push(Event::WPS_DPP_URI_Event, params);
}
```

---

**4. ap_manager — handle new WPS_DPP_URI_Event:**

```cpp
// ap_manager.cpp — add case in event handler (near line 3636)
case Event::WPS_DPP_URI_Event: {
    auto params = get_event_data<sWpsDppUriParams>(data);
    LOG(INFO) << "WPS-DPP-URI received from STA: " << params.sta_mac
              << " URI: " << params.uri;

    // Build new internal message — reuse existing message path to son_slave_thread
    auto msg = message_com::create_vs_message<
        beerocks_message::cACTION_APMANAGER_DPP_URI_NOTIFICATION>(cmdu_tx);
    msg->set_uri(params.uri);
    msg->sta_mac() = params.sta_mac;
    send_cmdu(m_slave_fd, cmdu_tx);   // REUSE existing socket
    break;
}
```

---

**5. son_slave_thread — route new message:**

```cpp
// son_slave_thread.cpp — add case near line 1803
case beerocks_message::ACTION_APMANAGER_DPP_URI_NOTIFICATION: {
    // forward to backhaul_manager via agent_fd — REUSE existing routing
    auto msg = beerocks_header->addClass<beerocks_message::cACTION_APMANAGER_DPP_URI_NOTIFICATION>();
    auto fwd = message_com::create_vs_message<
        beerocks_message::cACTION_BACKHAUL_DPP_URI_NOTIFICATION>(cmdu_tx);
    fwd->set_uri(msg->uri_str());
    fwd->sta_mac() = msg->sta_mac();
    send_cmdu(m_backhaul_manager_fd, cmdu_tx);
    break;
}
```

---

**6. BackhaulManager — receive URI and send 1905.1 to controller:**

```cpp
// backhaul_manager.cpp — handle new message
case beerocks_message::ACTION_BACKHAUL_DPP_URI_NOTIFICATION: {
    auto msg = beerocks_header->addClass<
        beerocks_message::cACTION_BACKHAUL_DPP_URI_NOTIFICATION>();

    // Build 1905.1 MAP message to controller — standard EasyMesh TLV
    if (!cmdu_tx.create(0,
        ieee1905_1::eMessageType::DPP_BOOTSTRAPPING_URI_NOTIFICATION_MESSAGE)) {
        LOG(ERROR) << "Failed to create DPP_BOOTSTRAPPING_URI_NOTIFICATION_MESSAGE";
        return false;
    }
    auto tlv = cmdu_tx.addClass<wfa_map::tlvDppBootstrappingUriNotification>();
    tlv->set_uri(msg->uri_str());
    tlv->sta_mac() = msg->sta_mac();

    auto db = AgentDB::get();
    send_cmdu_to_broker(cmdu_tx,
        db->controller_info.bridge_mac,
        db->bridge.mac);
    break;
}
```

---

**7. Controller dpp_protocol_task — receive and store:**

```cpp
// controller — new or existing DPP task
case ieee1905_1::eMessageType::DPP_BOOTSTRAPPING_URI_NOTIFICATION_MESSAGE: {
    auto tlv = cmdu_rx.getClass<wfa_map::tlvDppBootstrappingUriNotification>();

    std::string uri   = tlv->uri_str();
    std::string alias = "cpe-" + mac_to_hex_str(tlv->sta_mac());

    // YOUR EXISTING CODE — reused directly
    g_database->parse_dpp_bootstrap_info(alias, uri);

    // Write to TR-181 ProvisionedDPP — your existing data model
    dm_add_provisioned_dpp(alias, uri);

    // Continue EasyMesh DPP flow via proxy_agent_dpp_task (already exists)
    trigger_cce_and_dpp_auth(alias);
    break;
}
```

---

**Complete reuse summary:**

| Component | Action |
|---|---|
| `initiate_wps_pbc` NBAPI handler | Reuse — add DPP flag |
| `BackhaulManager::start_wps_pbc()` | Reuse — decision logic unchanged |
| `BackhaulManager::start_wps_pbc_ap()` | Reuse unchanged |
| `BackhaulManager::start_wps_pbc_sta()` | Extend — add `dpp_bootstrap_gen` + pass URI |
| `sta_wlan_hal->start_wps_pbc()` | Extend signature — add `dpp_uri=""` param |
| `ap_wlan_hal->start_wps_pbc()` | Extend — add `dpp_uri_no_cred` set before call |
| `WAIT_WPS` FSM state | Reuse unchanged |
| `WPS_Event_Timeout/Fail/Cancel` | Reuse unchanged |
| `cACTION_BACKHAUL_START_WPS_PBC_REQUEST` | Reuse unchanged |
| `cACTION_APMANAGER_START_WPS_PBC_REQUEST` | Reuse unchanged |
| `son_slave_thread` socket/routing | Reuse — add one new message case |
| `parse_dpp_bootstrap_info()` | Reuse — your existing code |
| `ProvisionedDPP` TR-181 | Reuse — your existing data model |
| `proxy_agent_dpp_task` | Reuse — handles CCE/DPP CMDUs already |
| **New** | `dpp_bootstrap_gen`, `dpp_bootstrap_get_uri` in BWL |
| **New** | `WPS_DPP_URI_Event` + handler in ap_manager |
| **New** | `cACTION_APMANAGER/BACKHAUL_DPP_URI_NOTIFICATION` messages |
| **New** | Controller `DPP_BOOTSTRAPPING_URI_NOTIFICATION_MESSAGE` handler |



Let me first evaluate the attached plan, then give the detailed implementation.

---

**Review of attached plan — what's gold vs what's wrong:**

**Gold (keep):**
- Phase A ODL subtree `DPP.` parallel to `WPS.` — correct architecture
- Phase B `mfn_wendpoint_dpp_start` function table pointer — consistent with pwhm FTA pattern
- Phase C exact sequence `DPP_BOOTSTRAP_GEN → DPP_GET_URI → store → notify` — correct
- Phase D session state struct — important for robustness
- Phase F clean split philosophy: *pwhm generates URI, prplMesh decides what to do* — architecturally sound

**Wrong / incomplete:**
- Phase E creates `initiate_dpp_pbc_auto()` separately — this DUPLICATES `initiate_wps_pbc_auto()` which we proved can be reused
- Phase E creates new `InitiateDPPPBC` NBAPI action — unnecessary, extend existing `InitiateWPSPBC`
- Entire plan misses `WPS-DPP-URI` event handling on the proxy AP side
- Phase F missing: how prplMesh subscribes to pwhm `DPP.URI` DM change event
- Phase G too vague — doesn't specify which TLVs, which messages, which files

---

Now the detailed plan with pseudocode:

---

## PHASE A — pwhm ODL + data model (interface contract first)

**Files:**
- `pwhm/odl/30_wld-defaults-eps.odl` — add DPP subtree
- `pwhm/include/wld/wld.h` — add session struct to `T_EndPoint`

**ODL change:**
```odl
// In endpoint object definition — parallel to existing WPS object
object DPP {
    /* DPP bootstrap URI generated by this endpoint */
    string URI = "";

    /* Idle | Generating | Ready | Error */
    string Status = "Idle";

    /* Internal bootstrap ID returned by DPP_BOOTSTRAP_GEN */
    uint32 BootstrapId = 0;

    /* Trigger DPP bootstrap generation — mirrors WPS.pushButton() */
    void pushButton();
}
```

**wld.h — add session struct and wire into T_EndPoint:**
```c
// new struct
typedef struct {
    bool            active;           // session in progress
    uint32_t        bootstrap_id;     // returned by DPP_BOOTSTRAP_GEN
    char            uri[2048];        // returned by DPP_GET_URI
    amxp_timer_t   *timer;            // timeout timer
    char            status[16];       // mirrors DM status string
} wld_dpp_session_t;

// add to T_EndPoint struct (alongside existing wps_session_t)
wld_dpp_session_t   dpp_session;
```

---

## PHASE B — pwhm endpoint layer

**Files:**
- `pwhm/src/wld_endpoint.c` — ODL write handler + entry function
- `pwhm/include/wld/wld_endpoint.h` — declare `wld_endpoint_dpp_start()`
- `pwhm/include/wld/wld.h` — add `mfn_wendpoint_dpp_start` to function table

**wld.h — function table addition:**
```c
// In T_CWLD_FUNC_TABLE (alongside mfn_wendpoint_wps_start)
swl_rc_ne (*mfn_wendpoint_dpp_start)(T_EndPoint *pEP);
```

**wld_endpoint.c — ODL handler + entry function:**
```c
// ODL pushButton() write handler — mirrors _pushButton() for WPS
static amxd_status_t _dppPushButton(amxd_object_t *obj,
                                     amxd_function_t *func,
                                     amxc_var_t *args,
                                     amxc_var_t *ret)
{
    T_EndPoint *pEP = wld_endpoint_fromObject(amxd_object_get_parent(obj));
    ASSERT_NOT_NULL(pEP, amxd_status_object_not_found, ME, "NULL");

    swl_rc_ne rc = wld_endpoint_dpp_start(pEP);
    return (rc == SWL_RC_OK) ? amxd_status_ok : amxd_status_unknown_error;
}

// Public entry function — mirrors wld_endpoint_wps_start()
swl_rc_ne wld_endpoint_dpp_start(T_EndPoint *pEP)
{
    ASSERT_NOT_NULL(pEP, SWL_RC_INVALID_PARAM, ME, "NULL");

    // Guard: endpoint must be enabled
    ASSERTI_TRUE(pEP->Enable, SWL_RC_INVALID_STATE, ME,
                 "%s: endpoint not enabled", pEP->Name);

    // Guard: reject if DPP session already active (mirrors WPS busy check)
    if (pEP->dpp_session.active) {
        SAH_TRACEZ_WARNING(ME, "%s: DPP session already active", pEP->Name);
        return SWL_RC_INVALID_STATE;
    }

    // Guard: reject if WPS session active (conflict)
    if (pEP->wps_session.active) {
        SAH_TRACEZ_WARNING(ME, "%s: WPS session active, DPP rejected", pEP->Name);
        return SWL_RC_INVALID_STATE;
    }

    // Set session state
    pEP->dpp_session.active = true;
    memset(pEP->dpp_session.uri, 0, sizeof(pEP->dpp_session.uri));
    pEP->dpp_session.bootstrap_id = 0;

    // Update DM status
    wld_endpoint_set_dpp_status(pEP, "Generating");

    // Call function table adapter
    ASSERT_NOT_NULL(pEP->pFA->mfn_wendpoint_dpp_start, SWL_RC_NOT_SUPPORTED, ME,
                    "%s: dpp_start not implemented", pEP->Name);

    swl_rc_ne rc = pEP->pFA->mfn_wendpoint_dpp_start(pEP);
    if (rc != SWL_RC_OK) {
        pEP->dpp_session.active = false;
        wld_endpoint_set_dpp_status(pEP, "Error");
    }
    return rc;
}

// DM helpers
static void wld_endpoint_set_dpp_status(T_EndPoint *pEP, const char *status)
{
    amxd_object_t *dpp_obj = amxd_object_get_child(pEP->dmObject, "DPP");
    if (dpp_obj) {
        amxd_object_set_value(cstring_t, dpp_obj, "Status", status);
    }
}

static void wld_endpoint_set_dpp_uri(T_EndPoint *pEP, const char *uri)
{
    amxd_object_t *dpp_obj = amxd_object_get_child(pEP->dmObject, "DPP");
    if (dpp_obj) {
        amxd_object_set_value(cstring_t, dpp_obj, "URI", uri);
        amxd_object_set_value(uint32_t, dpp_obj, "BootstrapId",
                              pEP->dpp_session.bootstrap_id);
    }
    // Setting URI fires dm:object-changed — prplMesh subscriber gets notified
}
```

---

## PHASE C — wpa_supplicant DPP command sequence

**Files:**
- `pwhm/src/nl80211/wld_wpaSupp_ep_api.c` — backend implementation
- `pwhm/src/Plugin/wifiGen_ep.c` — wire into generic plugin FTA

**wld_wpaSupp_ep_api.c:**
```c
swl_rc_ne wifiGen_ep_dppStart(T_EndPoint *pEP)
{
    ASSERT_NOT_NULL(pEP, SWL_RC_INVALID_PARAM, ME, "NULL");

    wld_wpaCtrlInterface_t *pIface = pEP->wpaCtrlIface;
    ASSERT_NOT_NULL(pIface, SWL_RC_INVALID_STATE, ME,
                    "%s: no wpa_ctrl interface", pEP->Name);
    ASSERTI_TRUE(wld_wpaCtrlInterface_isReady(pIface), SWL_RC_INVALID_STATE,
                 ME, "%s: wpa_ctrl not ready", pEP->Name);

    char reply[256] = {0};
    char cmd[256]   = {0};

    // Step 1 — DPP_BOOTSTRAP_GEN
    // get channel from radio (first available non-6GHz channel)
    const char *chan_str = s_get_dpp_chan_str(pEP);
    // get MAC from endpoint interface
    char mac_str[18] = {0};
    swl_mac_toString(pEP->pSSID->BSSID, mac_str, sizeof(mac_str));
    // remove colons for DPP format
    s_remove_colons(mac_str);

    snprintf(cmd, sizeof(cmd),
             "DPP_BOOTSTRAP_GEN type=qrcode chan=%s mac=%s",
             chan_str, mac_str);

    SAH_TRACEZ_INFO(ME, "%s: sending: %s", pEP->Name, cmd);
    if (!wld_wpaCtrl_sendCmdSynced(pIface, cmd, reply, sizeof(reply))) {
        SAH_TRACEZ_ERROR(ME, "%s: DPP_BOOTSTRAP_GEN failed", pEP->Name);
        pEP->dpp_session.active = false;
        wld_endpoint_set_dpp_status(pEP, "Error");
        return SWL_RC_ERROR;
    }

    // parse returned integer bootstrap ID
    int bootstrap_id = atoi(swl_str_trim(reply));
    if (bootstrap_id <= 0) {
        SAH_TRACEZ_ERROR(ME, "%s: invalid bootstrap_id: %s", pEP->Name, reply);
        pEP->dpp_session.active = false;
        wld_endpoint_set_dpp_status(pEP, "Error");
        return SWL_RC_ERROR;
    }
    pEP->dpp_session.bootstrap_id = (uint32_t)bootstrap_id;
    SAH_TRACEZ_INFO(ME, "%s: bootstrap_id=%d", pEP->Name, bootstrap_id);

    // Step 2 — DPP_GET_URI
    snprintf(cmd, sizeof(cmd), "DPP_GET_URI %d", bootstrap_id);
    memset(pEP->dpp_session.uri, 0, sizeof(pEP->dpp_session.uri));

    if (!wld_wpaCtrl_sendCmdSynced(pIface, cmd,
                                   pEP->dpp_session.uri,
                                   sizeof(pEP->dpp_session.uri))) {
        SAH_TRACEZ_ERROR(ME, "%s: DPP_GET_URI failed", pEP->Name);
        pEP->dpp_session.active = false;
        wld_endpoint_set_dpp_status(pEP, "Error");
        return SWL_RC_ERROR;
    }

    // validate URI starts with "DPP:"
    if (strncmp(pEP->dpp_session.uri, "DPP:", 4) != 0) {
        SAH_TRACEZ_ERROR(ME, "%s: invalid URI: %s", pEP->Name,
                         pEP->dpp_session.uri);
        pEP->dpp_session.active = false;
        wld_endpoint_set_dpp_status(pEP, "Error");
        return SWL_RC_ERROR;
    }

    SAH_TRACEZ_INFO(ME, "%s: DPP URI ready: %s", pEP->Name,
                    pEP->dpp_session.uri);

    // Step 3 — update DM (this fires dm:object-changed → prplMesh notified)
    wld_endpoint_set_dpp_uri(pEP, pEP->dpp_session.uri);
    wld_endpoint_set_dpp_status(pEP, "Ready");

    // Step 4 — start DPP_LISTEN so enrollee can receive auth request
    // (controller will send DPP auth after receiving URI via 1905.1)
    snprintf(cmd, sizeof(cmd), "DPP_LISTEN %s role=enrollee",
             s_get_dpp_listen_freq(pEP));
    wld_wpaCtrl_sendCmd(pIface, cmd);  // fire and forget

    return SWL_RC_OK;
}
```

---

## PHASE D — Session cleanup

**Files:**
- `pwhm/src/wld_endpoint.c` — add cleanup + timeout

```c
// Timeout handler — reuse amxp_timer pattern from WPS
static void s_dpp_timeout_cb(amxp_timer_t *timer, void *priv)
{
    T_EndPoint *pEP = (T_EndPoint *)priv;
    SAH_TRACEZ_WARNING(ME, "%s: DPP session timeout", pEP->Name);
    wld_endpoint_dpp_cleanup(pEP, "Timeout");
}

void wld_endpoint_dpp_cleanup(T_EndPoint *pEP, const char *reason)
{
    if (!pEP->dpp_session.active) {
        return;
    }
    SAH_TRACEZ_INFO(ME, "%s: DPP cleanup, reason: %s", pEP->Name, reason);

    // cancel any DPP_LISTEN in progress
    if (pEP->wpaCtrlIface) {
        wld_wpaCtrl_sendCmd(pEP->wpaCtrlIface, "DPP_STOP_LISTEN");
    }

    // stop timer
    if (pEP->dpp_session.timer) {
        amxp_timer_stop(pEP->dpp_session.timer);
    }

    // reset session
    pEP->dpp_session.active       = false;
    pEP->dpp_session.bootstrap_id = 0;
    memset(pEP->dpp_session.uri, 0, sizeof(pEP->dpp_session.uri));

    // update DM
    wld_endpoint_set_dpp_status(pEP, "Idle");
    wld_endpoint_set_dpp_uri(pEP, "");
}

// Call cleanup from existing endpoint reset paths —
// wherever wps_session cleanup is called, add dpp_cleanup too
// e.g. in wld_endpoint_sync_connection(), disconnect handlers
```

---

## PHASE E — prplMesh HAL bridge (reuse WPS path)

**Files:**
- `common/beerocks/bwl/whm/sta_wlan_hal_whm.cpp` — subscribe to DPP.URI event
- `common/beerocks/bwl/whm/sta_wlan_hal_whm.h` — new method declaration
- `agent/src/beerocks/slave/backhaul_manager/backhaul_manager.cpp` — extend existing start_wps_pbc_sta
- `agent/src/beerocks/slave/nbapi/on_action.cpp` — extend existing InitiateWPSPBC

**sta_wlan_hal_whm.cpp — subscribe to DPP.URI change:**
```cpp
// In sta_wlan_hal_whm::init() or attach() — alongside existing WPS event subscription
bool sta_wlan_hal_whm::subscribe_dpp_events()
{
    // subscribe to DPP.URI dm:object-changed on this endpoint
    // when pwhm sets DPP.URI, this fires
    std::string dpp_path = m_ep_path + "DPP.";

    m_ambiorix_cl.subscribe(dpp_path, "dm:object-changed",
        [this](const amxc_var_t *event_data) {
            const char *uri = GETP_CHAR(event_data, "parameters.URI.to");
            if (uri && uri[0] != '\0' && strncmp(uri, "DPP:", 4) == 0) {
                on_dpp_uri_ready(std::string(uri));
            }
        });
    return true;
}

void sta_wlan_hal_whm::on_dpp_uri_ready(const std::string &uri)
{
    LOG(INFO) << "DPP URI ready from pwhm: " << uri;

    // Store URI for use in next WPS_PBC call
    m_pending_dpp_uri = uri;

    // Now trigger WPS_PBC with DPP URI embedded
    // This reuses the ENTIRE existing WPS path — no new path needed
    start_wps_pbc_with_dpp_uri(uri);
}

// Extend existing start_wps_pbc — add optional dpp_uri param
bool sta_wlan_hal_whm::start_wps_pbc(const std::string &dpp_uri)
{
    std::string wps_path = m_ep_path + "WPS.";

    // check if already pairing — reuse existing guard
    // ... existing checks ...

    amxc_var_t args;
    amxc_var_init(&args);
    amxc_var_set_type(&args, AMXC_VAR_ID_HTABLE);

    if (!dpp_uri.empty()) {
        // pwhm will format: WPS_PBC multi_ap=1 dpp_uri=<uri>
        amxc_var_add_key(cstring_t, &args, "dpp_uri", dpp_uri.c_str());
        LOG(INFO) << "WPS_PBC with DPP URI embedded (len=" << dpp_uri.size() << ")";
    }
    // else: plain WPS_PBC multi_ap=1 — unchanged existing behavior

    amxc_var_t result;
    amxc_var_init(&result);
    bool ret = m_ambiorix_cl.call(wps_path, "pushButton", args, result);
    amxc_var_clean(&args);
    amxc_var_clean(&result);

    if (!ret) {
        LOG(ERROR) << "start_wps_pbc() failed!";
    }
    return ret;
}

// NEW: trigger DPP bootstrap generation in pwhm
bool sta_wlan_hal_whm::start_dpp_bootstrap()
{
    std::string dpp_path = m_ep_path + "DPP.";
    amxc_var_t args, result;
    amxc_var_init(&args);
    amxc_var_init(&result);

    bool ret = m_ambiorix_cl.call(dpp_path, "pushButton", args, result);
    // pwhm generates URI and fires DPP.URI event → on_dpp_uri_ready() called
    // → start_wps_pbc(uri) called automatically from there
    amxc_var_clean(&args);
    amxc_var_clean(&result);
    return ret;
}
```

**backhaul_manager.cpp — extend start_wps_pbc_sta, no new function:**
```cpp
// EXISTING function — extend, do NOT create a new one
bool BackhaulManager::start_wps_pbc_sta()
{
    auto sta_wlan_hal = get_selected_backhaul_sta_wlan_hal();
    if (!sta_wlan_hal) {
        LOG(ERROR) << "Failed to get backhaul STA hal";
        return false;
    }

    if (m_dpp_pbc_mode) {
        // DPP mode: trigger pwhm DPP bootstrap generation
        // pwhm will async notify via DPP.URI event
        // sta_wlan_hal_whm subscription handles the rest automatically
        LOG(INFO) << "DPP PBC mode: triggering DPP bootstrap generation";
        return sta_wlan_hal->start_dpp_bootstrap();
        // FSM still moves to WAIT_WPS — REUSED AS-IS
    }

    // EXISTING plain WPS path — unchanged
    if (!sta_wlan_hal->start_wps_pbc()) {
        LOG(ERROR) << "Failed to start wps";
        return false;
    }
    return true;
}

// EXISTING — extend to set DPP mode flag
bool BackhaulManager::initiate_wps_pbc_auto()
{
    // m_dpp_pbc_mode already set by caller before this is invoked
    // rest of logic unchanged — AP vs bSTA decision reused
    // ...existing code...
}
```

**on_action.cpp — extend existing NBAPI, no new action:**
```cpp
// EXISTING initiate_wps_pbc — extend with optional DPP mode
amxd_status_t initiate_wps_pbc(amxd_object_t *object,
                                amxd_function_t *func,
                                amxc_var_t *args,
                                amxc_var_t *ret)
{
    if (!g_wps_pbc_cb) {
        LOG(ERROR) << "WPS PBC: callback not set";
        return amxd_status_unknown_error;
    }

    // NEW: check for optional "dpp" argument
    // ba-cli: ubus call X_PRPLWARE-COM_Agent.WPS InitiateWPSPBC '{"dpp":true}'
    bool dpp_mode = false;
    const amxc_var_t *dpp_arg = GET_ARG(args, "dpp");
    if (dpp_arg) {
        dpp_mode = amxc_var_constcast(bool, dpp_arg);
    }

    g_wps_pbc_cb(dpp_mode);  // extend callback signature
    return amxd_status_ok;
}
```

---

## PHASE F — Proxy agent AP side (WPS-DPP-URI event)

**Files:**
- `common/beerocks/bwl/whm/ap_wlan_hal_whm.cpp` — parse WPS-DPP-URI event
- `common/beerocks/bwl/ap_wlan_hal.h` — new event enum + data struct
- `agent/src/beerocks/fronthaul_manager/ap_manager/ap_manager.cpp` — handle new event
- `common/beerocks/tlvf/` — new internal message (yaml → autogenerated)

**ap_wlan_hal.h — new event:**
```cpp
enum Event {
    // ... existing events ...
    WPS_Event_Timeout,
    WPS_Event_Fail,
    WPS_Event_Cancel,
    WPS_DPP_URI,        // NEW: WPS-DPP-URI received from hostapd
};

// NEW event data
struct sWpsDppUriParams {
    std::string uri;
    sMacAddr    sta_mac;
    std::string bssid;
};
```

**ap_wlan_hal_whm.cpp — parse raw WPS-DPP-URI from pwhm:**
```cpp
// In existing event processing callback (where WPS events are parsed)
// pwhm forwards the raw hostapd event string via fProcEvtMsg

void ap_wlan_hal_whm::process_raw_wpa_event(const std::string &event_str)
{
    // existing WPS event parsing...

    // NEW: parse WPS-DPP-URI
    // Format: WPS-DPP-URI uri=DPP:... bss=xx:xx:xx:xx:xx:xx sta=xx:xx:xx:xx:xx:xx
    if (event_str.find("WPS-DPP-URI") != std::string::npos) {
        sWpsDppUriParams params;

        // parse uri= field
        auto uri_pos = event_str.find("uri=");
        auto bss_pos = event_str.find(" bss=");
        if (uri_pos != std::string::npos && bss_pos != std::string::npos) {
            params.uri = event_str.substr(uri_pos + 4,
                                          bss_pos - (uri_pos + 4));
        }
        // parse sta= field
        auto sta_pos = event_str.find("sta=");
        if (sta_pos != std::string::npos) {
            params.sta_mac = tlvf::mac_from_string(
                event_str.substr(sta_pos + 4, 17));
        }

        if (!params.uri.empty()) {
            LOG(INFO) << "WPS-DPP-URI received from STA: " << params.sta_mac
                      << " URI length: " << params.uri.size();
            // push to event queue — same mechanism as WPS events
            event_queue_push(Event::WPS_DPP_URI,
                             std::make_shared<sWpsDppUriParams>(params));
        }
    }
}
```

**ap_manager.cpp — handle WPS_DPP_URI event:**
```cpp
// Add case alongside existing WPS event cases (near line 3636)
case Event::WPS_DPP_URI: {
    auto params = get_event_data<sWpsDppUriParams>(data);
    if (!params) {
        LOG(ERROR) << "WPS_DPP_URI: null event data";
        break;
    }
    LOG(INFO) << "WPS_DPP_URI event: sta=" << params->sta_mac
              << " uri_len=" << params->uri.size();

    // Build internal notification to son_slave_thread
    // Reuse existing cmdu_tx + send path
    auto msg = message_com::create_vs_message<
        beerocks_message::cACTION_APMANAGER_DPP_URI_NOTIFICATION>(cmdu_tx);
    if (!msg) {
        LOG(ERROR) << "Failed building DPP_URI_NOTIFICATION";
        break;
    }
    msg->set_uri(params->uri);
    msg->sta_mac() = params->sta_mac;

    // send to son_slave_thread — REUSE existing socket
    send_cmdu(m_slave_fd, cmdu_tx);
    break;
}
```

---

## PHASE G — son_slave_thread → backhaul_manager → controller

**son_slave_thread.cpp — route new message:**
```cpp
// Add case near existing ACTION_BACKHAUL_START_WPS_PBC_REQUEST (line 1803)
case beerocks_message::ACTION_APMANAGER_DPP_URI_NOTIFICATION: {
    auto in_msg = beerocks_header->addClass<
        beerocks_message::cACTION_APMANAGER_DPP_URI_NOTIFICATION>();
    if (!in_msg) {
        LOG(ERROR) << "addClass DPP_URI_NOTIFICATION failed";
        return false;
    }

    // Forward to backhaul_manager — REUSE existing m_backhaul_manager_fd socket
    auto out_msg = message_com::create_vs_message<
        beerocks_message::cACTION_BACKHAUL_DPP_URI_NOTIFICATION>(cmdu_tx);
    if (!out_msg) {
        LOG(ERROR) << "Failed building BACKHAUL_DPP_URI_NOTIFICATION";
        return false;
    }
    out_msg->set_uri(in_msg->uri_str());
    out_msg->sta_mac() = in_msg->sta_mac();

    LOG(DEBUG) << "DPP URI notification: forwarding to backhaul_manager";
    send_cmdu(m_backhaul_manager_fd, cmdu_tx);
    return true;
}
```

**backhaul_manager.cpp — send 1905.1 to controller:**
```cpp
// Add case in backhaul_manager message handler
case beerocks_message::ACTION_BACKHAUL_DPP_URI_NOTIFICATION: {
    auto msg = beerocks_header->addClass<
        beerocks_message::cACTION_BACKHAUL_DPP_URI_NOTIFICATION>();
    if (!msg) {
        LOG(ERROR) << "addClass BACKHAUL_DPP_URI_NOTIFICATION failed";
        return false;
    }

    std::string uri = msg->uri_str();
    LOG(INFO) << "DPP URI from proxy AP, STA: " << msg->sta_mac()
              << " URI len: " << uri.size();

    // Build 1905.1 MAP message to controller
    if (!cmdu_tx.create(0,
            ieee1905_1::eMessageType::DPP_BOOTSTRAPPING_URI_NOTIFICATION_MESSAGE)) {
        LOG(ERROR) << "Failed to create DPP_BOOTSTRAPPING_URI_NOTIFICATION";
        return false;
    }
    auto tlv = cmdu_tx.addClass<wfa_map::tlvDppBootstrappingUriNotification>();
    if (!tlv) {
        LOG(ERROR) << "Failed to add tlvDppBootstrappingUriNotification";
        return false;
    }
    tlv->set_uri(uri);
    tlv->enrollee_mac() = msg->sta_mac();

    auto db = AgentDB::get();
    // REUSE existing send path to controller
    send_cmdu_to_broker(cmdu_tx,
                        db->controller_info.bridge_mac,
                        db->bridge.mac);
    return true;
}
```
**Controller — handle URI notification (new or in existing DPP task):**
```Cpp
case ieee1905_1::eMessageType::DPP_BOOTSTRAPPING_URI_NOTIFICATION_MESSAGE: {
    auto tlv = cmdu_rx.getClass<wfa_map::tlvDppBootstrappingUriNotification>();
    if (!tlv) {
        LOG(ERROR) << "Missing tlvDppBootstrappingUriNotification";
        return false;
    }

    std::string uri = tlv->uri_str();
    sMacAddr enrollee_mac = tlv->enrollee_mac();

    // Generate alias from enrollee MAC
    std::string mac_str = tlvf::mac_to_string(enrollee_mac);
    mac_str.erase(std::remove(mac_str.begin(), mac_str.end(), ':'),
                  mac_str.end());
    std::string alias = "cpe-" + mac_str;

    LOG(INFO) << "DPP URI notification from agent, alias: " << alias;

    // YOUR EXISTING CODE — reused directly
    if (!g_database->parse_dpp_bootstrap_info(alias, uri)) {
        LOG(ERROR) << "Failed to parse DPP URI";
        return false;
    }

    // Write to TR-181 ProvisionedDPP — your existing data model
    // (create instance + set Alias + set DPPURI)
    dm_create_provisioned_dpp_entry(alias, uri);

    // Send CCE Indication to all agents
    // proxy_agent_dpp_task handles DPP CMDUs from here — REUSED
    send_dpp_cce_indication(true);

    return true;
}
```