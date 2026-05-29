// ============================================================
// controller_ucc_listener.cpp
// ============================================================

// SECTION 1 — around line 213, REMOVE this line:
//   auto &enrollee_bootstrapping_info = m_database.dpp_bootstrapping_info;
//
// ADD before the switch block (declare local struct):
        son::db::sDppBootstrappingInfo enrollee_bootstrapping_info;

// The switch block that fills enrollee_bootstrapping_info stays unchanged.

// ============================================================
// SECTION 2 — REPLACE the block after the switch/for loop
// ============================================================

// REMOVE (old logging + direct map access):
//        for (auto &ch : m_database.dpp_bootstrapping_info.operating_class_channel) {
//        LOG(DEBUG) << "mac:" << m_database.dpp_bootstrapping_info.mac;
//        ... etc
//        m_database.dpp_bootstrapping_map[alias] = std::move(enrollee_bootstrapping_info);

// REPLACE WITH:

        if (enrollee_bootstrapping_info.public_key.empty()) {
            err_string = "DPP bootstrapping data missing mandatory K (public key) field";
            return false;
        }

        std::string alias;
        auto alias_it = params.find("alias");
        if (alias_it != params.end() && !alias_it->second.empty()) {
            alias = alias_it->second;
        } else {
            std::string mac_str = tlvf::mac_to_string(enrollee_bootstrapping_info.mac);
            if (!mac_str.empty() && mac_str != "00:00:00:00:00:00") {
                std::string mac_clean;
                for (char c : mac_str) {
                    if (c != ':') mac_clean += c;
                }
                alias = "cpe-" + mac_clean;
            } else {
                alias = "cpe-" + enrollee_bootstrapping_info.public_key.substr(
                    0, std::min<size_t>(12, enrollee_bootstrapping_info.public_key.size()));
            }
            LOG(DEBUG) << "Auto-generated alias: " << alias;
        }
        enrollee_bootstrapping_info.alias = alias;

        std::string channels_str;
        for (auto &ch : enrollee_bootstrapping_info.operating_class_channel) {
            channels_str += std::to_string(ch.first) + "\\" + std::to_string(ch.second) + ",";
        }
        LOG(DEBUG) << "alias:"      << alias;
        LOG(DEBUG) << "channel:"    << channels_str;
        LOG(DEBUG) << "mac:"        << enrollee_bootstrapping_info.mac;
        LOG(DEBUG) << "info:"       << enrollee_bootstrapping_info.info;
        LOG(DEBUG) << "version:"    << (int)enrollee_bootstrapping_info.version;
        LOG(DEBUG) << "host:"       << enrollee_bootstrapping_info.host;
        LOG(DEBUG) << "public_key:" << enrollee_bootstrapping_info.public_key;

        m_database.add_dpp_bootstrapping_info(alias, std::move(enrollee_bootstrapping_info));

        return true;
