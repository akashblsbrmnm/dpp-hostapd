// ============================================================
// controller_ucc_listener.cpp — changes in handle_dev_exec_action
// ============================================================

// Coding style fixes applied:
//   - Coverity CID-30370: static const std::string for map lookup key
//   - curly braces on all single-line conditionals
//   - else on same line as closing brace
//   - if (!ptr) style
//   - alias moved into add_dpp_bootstrapping_info (not used after)

        // Local struct built here; stored in db only after full validation
        son::db::sDppBootstrappingInfo enrollee_bootstrapping_info;

        // ... existing switch/parse loop unchanged ...

        if (enrollee_bootstrapping_info.public_key.empty()) {
            err_string = "DPP bootstrapping data missing mandatory K (public key) field";
            return false;
        }

        // Determine alias: use provided value or auto-generate from MAC/public key.
        // TR-181: CPE-generated aliases MUST start with "cpe-" prefix.
        std::string alias;

        // Coverity CID-30370: use static const string to avoid temporary construction
        static const std::string kAliasKey = "alias";
        auto alias_it = params.find(kAliasKey);

        if (alias_it != params.end() && !alias_it->second.empty()) {
            alias = alias_it->second;
        } else {
            std::string mac_str = tlvf::mac_to_string(enrollee_bootstrapping_info.mac);
            if (!mac_str.empty() && mac_str != "00:00:00:00:00:00") {
                std::string mac_clean;
                for (char c : mac_str) {
                    if (c != ':') {
                        mac_clean += c;
                    }
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
        for (const auto &ch : enrollee_bootstrapping_info.operating_class_channel) {
            channels_str +=
                std::to_string(ch.first) + "\\" + std::to_string(ch.second) + ",";
        }
        LOG(DEBUG) << "alias:"      << alias;
        LOG(DEBUG) << "channel:"    << channels_str;
        LOG(DEBUG) << "mac:"        << enrollee_bootstrapping_info.mac;
        LOG(DEBUG) << "info:"       << enrollee_bootstrapping_info.info;
        LOG(DEBUG) << "version:"    << static_cast<int>(enrollee_bootstrapping_info.version);
        LOG(DEBUG) << "host:"       << enrollee_bootstrapping_info.host;
        LOG(DEBUG) << "public_key:" << enrollee_bootstrapping_info.public_key;

        m_database.add_dpp_bootstrapping_info(alias, enrollee_bootstrapping_info);

        return true;
