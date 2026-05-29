// ============================================================
// db.cpp — REPLACE calculate_dpp_bootstrapping_str entirely
// ============================================================

std::string db::calculate_dpp_bootstrapping_str(const sDppBootstrappingInfo &info)
{
    std::string dpp_conn_string;
    std::string opclass_channel_str;

    for (const auto &ch : info.operating_class_channel) {
        opclass_channel_str += std::to_string(ch.first) + "/" + std::to_string(ch.second) + ",";
    }
    if (!opclass_channel_str.empty()) {
        opclass_channel_str.pop_back();
        dpp_conn_string += "C:" + opclass_channel_str + ";";
    }

    if (info.mac != net::network_utils::ZERO_MAC) {
        std::string mac_string = tlvf::mac_to_string(info.mac);
        mac_string.erase(std::remove(mac_string.begin(), mac_string.end(), ':'),
                         mac_string.end());
        dpp_conn_string += "M:" + mac_string + ";";
    }

    if (!info.info.empty())
        dpp_conn_string += "I:" + info.info + ";";

    if (info.version != 0)
        dpp_conn_string += "V:" + std::to_string(info.version) + ";";

    if (!info.host.empty())
        dpp_conn_string += "H:" + info.host + ";";

    if (!info.public_key.empty())
        dpp_conn_string += "K:" + info.public_key + ";";

    if (dpp_conn_string.empty()) {
        return "";
    }

    return "DPP:" + dpp_conn_string + ";;";
}

// ============================================================
// db.cpp — ADD these new functions at end of file
// ============================================================

bool db::dpp_uri_parse(const std::string &alias, const std::string &dpp_uri)
{
    LOG(DEBUG) << "dpp_uri_parse called with alias: '"
               << alias << "', dpp_uri: '" << dpp_uri << "'";

    const std::string prefix = "DPP:";
    if (dpp_uri.compare(0, prefix.size(), prefix) != 0) {
        LOG(ERROR) << "Invalid DPP URI prefix, expected 'DPP:'";
        return false;
    }

    const std::string suffix = ";;";
    if (dpp_uri.size() < suffix.size() ||
        dpp_uri.compare(dpp_uri.size() - suffix.size(), suffix.size(), suffix) != 0) {
        LOG(ERROR) << "Invalid DPP URI suffix, expected ';;'";
        return false;
    }

    std::string body = dpp_uri.substr(prefix.size(),
                           dpp_uri.size() - prefix.size() - suffix.size());
    LOG(DEBUG) << "DPP URI body after strip: '" << body << "'";

    sDppBootstrappingInfo dpp_info;
    dpp_info.alias = alias;

    std::stringstream ss(body);
    std::string token;
    while (std::getline(ss, token, ';')) {
        if (token.empty()) {
            LOG(DEBUG) << "Empty token skipped";
            continue;
        }

        LOG(DEBUG) << "Processing token: '" << token << "'";

        auto sep = token.find(':');
        if (sep == std::string::npos) {
            LOG(WARNING) << "Token missing ':' separator, skipping: '" << token << "'";
            continue;
        }

        std::string key = token.substr(0, sep);
        std::string val = token.substr(sep + 1);
        LOG(DEBUG) << "Parsed key: '" << key << "', val: '" << val << "'";

        if (key == "C") {
            std::stringstream cs(val);
            std::string entry;
            while (std::getline(cs, entry, ',')) {
                auto slash = entry.find('/');
                if (slash == std::string::npos) {
                    LOG(WARNING) << "Channel entry missing '/', skipping: '" << entry << "'";
                    continue;
                }
                try {
                    uint8_t op_class = static_cast<uint8_t>(std::stoi(entry.substr(0, slash)));
                    uint8_t channel  = static_cast<uint8_t>(std::stoi(entry.substr(slash + 1)));
                    dpp_info.operating_class_channel.emplace(op_class, channel);
                    LOG(DEBUG) << "Added op_class: " << (int)op_class
                               << ", channel: " << (int)channel;
                } catch (const std::exception &e) {
                    LOG(ERROR) << "Exception parsing C field entry '" << entry
                               << "': " << e.what();
                }
            }
        } else if (key == "M") {
            std::string mac_with_colons;
            for (size_t i = 0; i < val.size(); i += 2) {
                if (i > 0) mac_with_colons += ':';
                mac_with_colons += val.substr(i, 2);
            }
            dpp_info.mac = tlvf::mac_from_string(mac_with_colons);
            LOG(DEBUG) << "Parsed MAC: " << dpp_info.mac;
        } else if (key == "I") {
            dpp_info.info = val;
            LOG(DEBUG) << "Parsed info: '" << dpp_info.info << "'";
        } else if (key == "V") {
            try {
                dpp_info.version = static_cast<uint8_t>(std::stoi(val));
                LOG(DEBUG) << "Parsed version: " << (int)dpp_info.version;
            } catch (const std::exception &e) {
                LOG(ERROR) << "Exception parsing V field '" << val << "': " << e.what();
            }
        } else if (key == "H") {
            dpp_info.host = val;
            LOG(DEBUG) << "Parsed host: '" << dpp_info.host << "'";
        } else if (key == "K") {
            dpp_info.public_key = val;
            LOG(DEBUG) << "Parsed public_key length: "
                       << dpp_info.public_key.size() << " chars";
        } else {
            LOG(DEBUG) << "Unknown key '" << key << "' ignored";
        }
    }

    if (dpp_info.public_key.empty()) {
        LOG(ERROR) << "DPP URI missing mandatory K (public key) for alias: " << alias;
        return false;
    }

    bool overwrite = (dpp_bootstrapping_map.find(alias) != dpp_bootstrapping_map.end());
    dpp_bootstrapping_map[alias] = std::move(dpp_info);

    if (overwrite) {
        LOG(DEBUG) << "Overwrote existing DPP entry for alias: " << alias
                   << ", total entries: " << dpp_bootstrapping_map.size();
    } else {
        LOG(DEBUG) << "Inserted new DPP entry for alias: " << alias
                   << ", total entries: " << dpp_bootstrapping_map.size();
    }

    return true;
}

void db::add_dpp_bootstrapping_info(const std::string &alias, sDppBootstrappingInfo info)
{
    dpp_bootstrapping_map[alias] = std::move(info);
    LOG(DEBUG) << "Stored DPP bootstrapping info for alias: " << alias
               << ", total entries: " << dpp_bootstrapping_map.size();
}

void db::remove_dpp_bootstrapping_info(const std::string &alias)
{
    auto it = dpp_bootstrapping_map.find(alias);
    if (it != dpp_bootstrapping_map.end()) {
        dpp_bootstrapping_map.erase(it);
        LOG(DEBUG) << "Removed DPP bootstrapping info for alias: " << alias;
    } else {
        LOG(WARNING) << "DPP bootstrapping info not found for alias: " << alias;
    }
}

const db::sDppBootstrappingInfo *db::get_dpp_info_by_mac(const sMacAddr &mac) const
{
    for (const auto &entry : dpp_bootstrapping_map) {
        if (entry.second.mac == mac) {
            LOG(DEBUG) << "Found DPP bootstrapping info for MAC: " << mac
                       << ", alias: " << entry.first;
            return &entry.second;
        }
    }
    LOG(DEBUG) << "No DPP bootstrapping info found for MAC: " << mac;
    return nullptr;
}
