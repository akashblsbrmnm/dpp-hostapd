// ============================================================
// db.h — FIND AND REPLACE the sDppBootstrappingInfo block
// ============================================================

// REMOVE this entire block (old):
//    struct sDppBootstrappingInfo {
//        std::multimap<uint8_t, uint8_t> operating_class_channel;
//        sMacAddr mac;
//        std::string info;
//        uint8_t version = 0;
//        std::string host;
//        std::string public_key;
//    } dpp_bootstrapping_info;

// ALSO REMOVE from public section (if present):
//    std::unordered_map<std::string, sDppBootstrappingInfo> dpp_bootstrapping_map;

// ALSO REMOVE from private section (if present):
//    bool dpp_uri_parse(const std::string &alias, const std::string &dpp_uri);
//    const std::unordered_map<std::string, sDppBootstrappingInfo> &get_dpp_bootstrapping_map() const;

// ============================================================
// ADD in PUBLIC section of class db:
// ============================================================

    struct sDppBootstrappingInfo {
        std::string alias;
        std::multimap<uint8_t, uint8_t> operating_class_channel;
        sMacAddr mac;
        std::string info;
        uint8_t version = 0;
        std::string host;
        std::string public_key;
    };

    bool dpp_uri_parse(const std::string &alias, const std::string &dpp_uri);
    void add_dpp_bootstrapping_info(const std::string &alias, sDppBootstrappingInfo info);
    void remove_dpp_bootstrapping_info(const std::string &alias);
    const sDppBootstrappingInfo *get_dpp_info_by_mac(const sMacAddr &mac) const;
    static std::string calculate_dpp_bootstrapping_str(const sDppBootstrappingInfo &info);

// ============================================================
// ADD in PRIVATE section of class db (near m_dialog_tokens):
// ============================================================

    std::unordered_map<std::string, sDppBootstrappingInfo> dpp_bootstrapping_map;
