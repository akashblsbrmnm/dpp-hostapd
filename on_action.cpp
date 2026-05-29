// ============================================================
// on_action.cpp
// REPLACE event_provisioned_dpp_changed
// ADD event_provisioned_dpp_removed
// UPDATE get_events_list
// ============================================================

static void event_provisioned_dpp_changed(const char *const sig_name, const amxc_var_t *const data,
                                          void *const priv)
{
    LOG(DEBUG) << "event_provisioned_dpp_changed fired, sig_name: " << sig_name;

    amxd_object_t *dpp_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!dpp_obj) {
        LOG(WARNING) << "Failed to get ProvisionedDPP object";
        return;
    }
    LOG(DEBUG) << "ProvisionedDPP object retrieved successfully";

    const char *alias = amxd_object_get_cstring_t(dpp_obj, "Alias", nullptr);
    LOG(DEBUG) << "Read Alias from DM object: '" << (alias ? alias : "null") << "'";

    if (!alias || alias[0] == '\0') {
        LOG(WARNING) << "ProvisionedDPP Alias is empty, skipping";
        return;
    }

    const char *dpp_uri = amxd_object_get_cstring_t(dpp_obj, "DPPURI", nullptr);
    LOG(DEBUG) << "Read DPPURI from DM object: '" << (dpp_uri ? dpp_uri : "null") << "'";

    if (!dpp_uri || dpp_uri[0] == '\0') {
        LOG(WARNING) << "DPPURI is empty for alias: " << alias;
        return;
    }

    LOG(DEBUG) << "Calling dpp_uri_parse for alias: " << alias;
    if (!g_database->dpp_uri_parse(std::string(alias), std::string(dpp_uri))) {
        LOG(ERROR) << "Failed to parse DPPURI for alias: " << alias;
        return;
    }
    LOG(DEBUG) << "dpp_uri_parse succeeded for alias: " << alias;
}

static void event_provisioned_dpp_removed(const char *const sig_name, const amxc_var_t *const data,
                                          void *const priv)
{
    LOG(DEBUG) << "event_provisioned_dpp_removed fired";

    amxd_object_t *dpp_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!dpp_obj) {
        LOG(WARNING) << "Failed to get ProvisionedDPP object on removal";
        return;
    }

    const char *alias = amxd_object_get_cstring_t(dpp_obj, "Alias", nullptr);
    if (!alias || alias[0] == '\0') {
        LOG(WARNING) << "ProvisionedDPP Alias is empty on removal, skipping";
        return;
    }

    LOG(DEBUG) << "Removing DPP bootstrapping info for alias: " << alias;
    g_database->remove_dpp_bootstrapping_info(std::string(alias));
}

// ============================================================
// REPLACE get_events_list — add both DPP handlers
// ============================================================

std::vector<beerocks::nbapi::sEvents> get_events_list(void)
{
    const std::vector<beerocks::nbapi::sEvents> events_list = {
        {"event_configuration_changed", event_configuration_changed},
        {"event_traffic_separation_changed", event_traffic_separation_changed},
        {"event_network_group_changed", event_network_group_changed},
        {"event_network_enable_changed", event_network_enable_changed},
        {"event_provisioned_dpp_changed", event_provisioned_dpp_changed},
        {"event_provisioned_dpp_removed", event_provisioned_dpp_removed}};
    return events_list;
}
