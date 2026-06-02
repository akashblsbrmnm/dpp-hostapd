// ============================================================
// db.h — changes only (not full file)
// ============================================================

// REMOVE old single-instance typedef struct:
//
//    struct sDppBootstrappingInfo {
//        std::multimap<uint8_t, uint8_t> operating_class_channel;
//        sMacAddr mac;
//        std::string info;
//        uint8_t version = 0;
//        std::string host;
//        std::string public_key;
//    } dpp_bootstrapping_info;

// ADD in public section:

    // Coding style: struct (not typedef struct), camelcase with s prefix,
    // members in lower_snake_case, no private/protected members
    struct sDppBootstrappingInfo {
        std::string alias;
        std::multimap<uint8_t, uint8_t> operating_class_channel;
        sMacAddr mac;
        std::string info;
        uint8_t version = 0;
        std::string host;
        std::string public_key;
    };

    // Coding style: const-ref for args > 32 bits
    void add_dpp_bootstrapping_info(const std::string &alias,
                                    const sDppBootstrappingInfo &info);
    bool dpp_uri_parse(const std::string &alias, const std::string &dpp_uri);
    const sDppBootstrappingInfo *get_dpp_info_by_mac(const sMacAddr &mac) const;
    static std::string calculate_dpp_bootstrapping_str(const sDppBootstrappingInfo &info);

// ADD in private section (near m_dialog_tokens):

    // Stores DPP bootstrapping info keyed by Alias.
    // TR-181: at most one entry per Alias value.
    std::unordered_map<std::string, sDppBootstrappingInfo> dpp_bootstrapping_map;
