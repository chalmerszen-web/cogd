#pragma once
// Invented protocol fixture; not copied from an account or SDK allocation.
// PIN_HEX is replaced by a test-selected synthetic hash string.
static constexpr char kSyntheticAllocation[] =
    "{\"sid\":\"synthetic-sid\",\"aoqTokenForClient\":\"synthetic-token\","
    "\"clientRelayCertFingerprint\":\"PIN_HEX\","
    "\"clientRelayEndpoints\":[{\"endpoint\":\"relay.example.invalid\",\"port\":8443,\"route_index\":0}],"
    "\"sidExpiresInSecs\":60,\"extraInfo\":{\"workspaceIdHash\":\"synthetic-workspace\"}}";
