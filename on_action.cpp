// ============================================================
// on_action.cpp — event_provisioned_dpp_changed + get_events_list
// ============================================================

// Coding style fixes applied:
//   - if (!ptr) instead of ptr == nullptr
//   - curly braces on all conditionals
//   - else on same line as closing brace
//   - const-ref for string args

static void event_provisioned_dpp_changed(const char *const sig_name, const amxc_var_t *const data,
                                          void *const priv)
{
    LOG(INFO) << "event_provisioned_dpp_changed fired, sig_name: " << sig_name;

    amxd_object_t *dpp_obj =
        amxd_dm_signal_get_object(beerocks::nbapi::Amxrt::getDatamodel(), data);

    if (!dpp_obj) {
        LOG(WARNING) << "Failed to get ProvisionedDPP object";
        return;
    }

    const char *alias = amxd_object_get_cstring_t(dpp_obj, "Alias", nullptr);
    LOG(INFO) << "Read Alias from DM object: '" << (alias ? alias : "null") << "'";

    if (!alias || alias[0] == '\0') {
        LOG(WARNING) << "ProvisionedDPP Alias is empty, skipping";
        return;
    }

    const char *dpp_uri = amxd_object_get_cstring_t(dpp_obj, "DPPURI", nullptr);
    LOG(INFO) << "Read DPPURI from DM object: '" << (dpp_uri ? dpp_uri : "null") << "'";

    if (!dpp_uri || dpp_uri[0] == '\0') {
        LOG(WARNING) << "DPPURI is empty for alias: " << alias;
        return;
    }

    if (!g_database->dpp_uri_parse(std::string(alias), std::string(dpp_uri))) {
        LOG(ERROR) << "Failed to parse DPPURI for alias: " << alias;
        return;
    }

    LOG(INFO) << "dpp_uri_parse succeeded for alias: " << alias;
}

static void event_provisioned_dpp_removed(const char *const sig_name, const amxc_var_t *const data,
                                          void *const priv)
{
    LOG(INFO) << "event_provisioned_dpp_removed fired";

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

    g_database->remove_dpp_bootstrapping_info(std::string(alias));
}

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
