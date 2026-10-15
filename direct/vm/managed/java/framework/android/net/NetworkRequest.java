package android.net;

public class NetworkRequest {
    NetworkRequest() {}

    public static class Builder {
        public Builder() {}

        public Builder addCapability(int capability) {
            return this;
        }

        public Builder addTransportType(int transportType) {
            return this;
        }

        public NetworkRequest build() {
            return new NetworkRequest();
        }
    }
}
