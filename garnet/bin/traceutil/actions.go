package main

import (
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/golang/glog"
)

const defaultCategories = "app,benchmark,gfx,input,kernel:meta,kernel:sched,ledger,magma,modular,motown,view,flutter,dart,dart:compiler,dart:dart,dart:debugger,dart:embedder,dart:gc,dart:isolate,dart:profiler,dart:vm"

type captureTraceConfig struct {
	Categories           string
	BufferSize           uint // [MB]
	BufferingMode        string
	Duration             time.Duration
	// Command is set differently as it is passed via positional args.
	Command              []string
	BenchmarkResultsFile string
	SpecFile             string
	Binary               bool
	Stream               bool
	Compress             bool
	Detach               bool
	Decouple             bool
	Spawn                bool
}

func newCaptureTraceConfig(f *flag.FlagSet) *captureTraceConfig {
	config := &captureTraceConfig{}

	// TODO(TO-400): It would be nice to be able to specify +category or
	// -category to add or subtract from the default set.
	f.StringVar(&config.Categories, "categories", defaultCategories,
		"Comma separated list of categories to trace. \"all\" for all categories.")
	f.UintVar(&config.BufferSize, "buffer-size", 0,
		"Size of trace buffer in MB.")
	f.StringVar(&config.BufferingMode, "buffering-mode", "oneshot",
		"Buffering mode (one of: oneshot,circular,streaming.")
	f.DurationVar(&config.Duration, "duration", 0,
		"Duration of trace capture (e.g. '10s').  Second resolution.")
	f.StringVar(&config.SpecFile, "spec-file", "",
		"Tracing specification file.")
	f.StringVar(
		&config.BenchmarkResultsFile,
		"benchmark-results-file",
		"",
		"Relative filepath for storing benchmark results.",
	)
	f.BoolVar(&config.Binary, "binary", false,
		"Capture trace in binary format on the target.")
	f.BoolVar(&config.Stream, "stream", false,
		"Stream trace output to a local file, instead of saving to target disk and then copying it.")
	f.BoolVar(&config.Compress, "compress", false,
		"Compress the trace output before writing to disk. This option is currently ignored if --stream is specified.")
	f.BoolVar(&config.Detach, "detach", false,
		"Don't stop the traced program when tracing finished.")
	f.BoolVar(&config.Decouple, "decouple", false,
		"Don't stop tracing when the traced program exits.")
	f.BoolVar(&config.Spawn, "spawn", false,
		"Use fdio_spawn to run a legacy app. Detach will have no effect when using this option.")

	return config
}

func streamTraceOutput(listener net.Listener, traceOutput *os.File, streamStatus chan<- error) {
	fmt.Printf("Listening for remote connection: %s\n", listener.Addr().(*net.TCPAddr).String())
	conn, err := listener.Accept()
	if err != nil {
		streamStatus <- fmt.Errorf("Unable to accept incoming trace connection: %s", err.Error())
		return
	}
	fmt.Printf("Incoming trace stream connected\n")
	nBytes, err := io.Copy(traceOutput, conn)
	conn.Close()
	if err != nil {
		streamStatus <- fmt.Errorf("Error writing trace results: %s", err.Error())
	} else {
		fmt.Printf("Wrote trace output, %d bytes\n", nBytes)
		streamStatus <- nil
	}
}

func captureTrace(config *captureTraceConfig, conn *TargetConnection, traceOutput *os.File) error {
	cmd := []string{"trace"}
	// Pass on our verbosity level to trace.
	if glog.V(2) {
		cmd = append(cmd, "--verbose=2")
	} else if glog.V(1) {
		cmd = append(cmd, "--verbose=1")
	}
	cmd = append(cmd, "record")

	if config.Categories == "" {
		config.Categories = defaultCategories
	}

	if len(config.SpecFile) > 0 {
		cmd = append(cmd, "--spec-file="+config.SpecFile)
	}

	if len(config.BenchmarkResultsFile) > 0 {
		cmd = append(cmd, "--benchmark-results-file="+config.BenchmarkResultsFile)
	}

	if config.Categories != "all" {
		cmd = append(cmd, "--categories="+config.Categories)
	}

	if config.BufferSize != 0 {
		cmd = append(cmd, "--buffer-size="+
			strconv.FormatUint(uint64(config.BufferSize), 10))
	}
	if config.BufferingMode == "oneshot" ||
		config.BufferingMode == "circular" ||
		config.BufferingMode == "streaming" {
		cmd = append(cmd, "--buffering-mode="+config.BufferingMode)
	} else {
		return fmt.Errorf("Invalid value for --buffering-mode: %s",
			config.BufferingMode)
	}
	if config.Duration != 0 {
		cmd = append(cmd, "--duration="+
			strconv.FormatUint(uint64(config.Duration.Seconds()), 10))
	}
	if config.Binary {
		cmd = append(cmd, "--binary")
	}
	if config.Compress {
		cmd = append(cmd, "--compress")
	}

	var listener net.Listener
	var err error
	streamChan := make(chan error)
	if config.Stream {
		listener, err = conn.client.ListenTCP(&net.TCPAddr{IP: net.IPv6loopback})
		if err != nil {
			return err
		}
		cmd = append(cmd, "--output-file=tcp:"+listener.Addr().(*net.TCPAddr).String())
		defer func() {
			if listener != nil {
				glog.Info("Closing trace stream listener")
				listener.Close()
			}
		}()
		go streamTraceOutput(listener, traceOutput, streamChan)
	}

	if config.Detach {
		cmd = append(cmd, "--detach")
	}
	if config.Decouple {
		cmd = append(cmd, "--decouple")
	}
	if config.Spawn {
		cmd = append(cmd, "--spawn")
	}

	// The program to run must appear last.
	// TODO(PT-130): The handling of embedded spaces in the command
	// and its arguments could be better.
	if len(config.Command) > 0 {
		// There is a difference between the trace failing and the
		// child failing, we need to know if trace fails.
		// TODO(PT-95): If we talk to trace-manager directly to get
		// trace data and then talk to some other service to launch the
		// child then there's no conflating of return codes.
		cmd = append(cmd, "--return-child-result=false")
		cmd = append(cmd, config.Command...)
	}

	// TODO(TO-401): The target `trace` command's output is misleading.
	// Specifically it says "Trace file written to /tmp/trace.json" which
	// references where the file is written to on the target not on the host.
	// We should wrap this and offer less confusing status.
	err = conn.RunCommand(strings.Join(cmd, " "))
	if err != nil {
		return err
	}

	// Check for an error during streaming.
	if config.Stream {
		fmt.Printf("Waiting to finish receiving trace stream ...\n")
		err = <-streamChan
	}

	return err
}

func convertTrace(generator string, outputPath string,
	title string, inputPaths ...string) error {
	fmt.Printf("Converting %v to %s... ", inputPaths, outputPath)
	var args []string
	args = append(args, "--output="+outputPath)
	if title != "" {
		args = append(args, "--title="+title)
	}
	args = append(args, inputPaths...)
	err := runCommand(generator, args)
	if err != nil {
		fmt.Printf("failed: %s\n", err.Error())
		fmt.Printf("Invoked as: %s %s\n", generator, args)
	} else {
		fmt.Println("done.")
	}
	return err
}
