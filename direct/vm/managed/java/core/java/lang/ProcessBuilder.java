package java.lang;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Builds the command, but start() fails like Runtime.exec (spec 10.3: no fork/exec). */
public final class ProcessBuilder {
    private List<String> command;
    private java.io.File directory;
    private boolean redirectErrorStream;

    public ProcessBuilder(List<String> command) {
        if (command == null) throw new NullPointerException();
        this.command = command;
    }

    public ProcessBuilder(String... command) {
        this.command = new ArrayList<String>(Arrays.asList(command));
    }

    public ProcessBuilder command(List<String> command) {
        if (command == null) throw new NullPointerException();
        this.command = command;
        return this;
    }

    public ProcessBuilder command(String... command) {
        this.command = new ArrayList<String>(Arrays.asList(command));
        return this;
    }

    public List<String> command() {
        return command;
    }

    public java.io.File directory() {
        return directory;
    }

    public ProcessBuilder directory(java.io.File directory) {
        this.directory = directory;
        return this;
    }

    public boolean redirectErrorStream() {
        return redirectErrorStream;
    }

    public ProcessBuilder redirectErrorStream(boolean redirectErrorStream) {
        this.redirectErrorStream = redirectErrorStream;
        return this;
    }

    public Process start() throws java.io.IOException {
        String program = command.isEmpty() ? "" : command.get(0);
        throw new java.io.IOException("Cannot run program \"" + program + "\": process creation is not supported");
    }
}
