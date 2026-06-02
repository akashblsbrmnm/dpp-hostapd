// ============================================================
// vbss_actions.cpp — in create_vbss(), replace set_dpp_connector call
// ============================================================

// Coding style fixes applied:
//   - else on same line as closing brace
//   - if (!ptr) style
//   - curly braces on all conditionals

    const auto *dpp_info = database.get_dpp_info_by_mac(client_vbss.client_mac);
    if (dpp_info) {
        vbss_creation_req->set_dpp_connector(
            son::db::calculate_dpp_bootstrapping_str(*dpp_info));
    } else {
        LOG(DEBUG) << "No DPP bootstrapping info found for client MAC: "
                   << client_vbss.client_mac << ", skipping DPP connector";
    }
