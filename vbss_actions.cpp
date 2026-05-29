// ============================================================
// vbss_actions.cpp — in create_vbss()
// ============================================================

// REMOVE (old single-instance call):
//    vbss_creation_req->set_dpp_connector(database.calculate_dpp_bootstrapping_str());

// REPLACE WITH:
    const auto *dpp_info = database.get_dpp_info_by_mac(client_vbss.client_mac);
    if (dpp_info) {
        vbss_creation_req->set_dpp_connector(
            son::db::calculate_dpp_bootstrapping_str(*dpp_info));
    } else {
        LOG(DEBUG) << "No DPP bootstrapping info found for client MAC: "
                   << client_vbss.client_mac << ", skipping DPP connector";
    }
