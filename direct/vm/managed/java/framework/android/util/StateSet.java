package android.util;

/** Drawable state sets: +attr means "must be set", -attr "must not be set". */
public class StateSet {
    public static final int[] WILD_CARD = new int[0];
    public static final int[] NOTHING = new int[] {0};

    private StateSet() {}

    public static boolean isWildCard(int[] stateSetOrSpec) {
        return stateSetOrSpec.length == 0 || stateSetOrSpec[0] == 0;
    }

    public static boolean stateSetMatches(int[] stateSpec, int[] stateSet) {
        if (stateSet == null) return stateSpec == null || isWildCard(stateSpec);
        final int stateSpecSize = stateSpec.length;
        final int stateSetSize = stateSet.length;
        for (int i = 0; i < stateSpecSize; i++) {
            final int stateSpecState = stateSpec[i];
            if (stateSpecState == 0) return true;  // wild-card terminator
            final boolean mustMatch = stateSpecState > 0;
            final int state = mustMatch ? stateSpecState : -stateSpecState;
            boolean found = false;
            for (int j = 0; j < stateSetSize; j++) {
                final int s = stateSet[j];
                if (s == 0) break;
                if (s == state) {
                    found = true;
                    break;
                }
            }
            if (found != mustMatch) return false;
        }
        return true;
    }

    public static boolean stateSetMatches(int[] stateSpec, int state) {
        return stateSetMatches(stateSpec, new int[] {state});
    }

    public static int[] trimStateSet(int[] states, int newSize) {
        if (states.length == newSize) return states;
        int[] trimmed = new int[newSize];
        System.arraycopy(states, 0, trimmed, 0, newSize);
        return trimmed;
    }

    public static String dump(int[] states) {
        StringBuilder sb = new StringBuilder();
        for (int s : states) sb.append(s).append(' ');
        return sb.toString();
    }
}
