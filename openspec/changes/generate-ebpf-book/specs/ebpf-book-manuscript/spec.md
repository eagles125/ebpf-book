## ADDED Requirements

### Requirement: Manuscript scaffold

The repository SHALL contain a `book/` manuscript scaffold for the Chinese eBPF self-study book, with a top-level README, a style guide, chapter files, appendix files, and example directories as needed.

#### Scenario: First-batch scaffold exists

- **WHEN** the first batch is implemented
- **THEN** `book/README.md`, `book/STYLE.md`, `book/chapters/`, and `book/appendix/` SHALL exist

### Requirement: Complete table of contents

The manuscript README SHALL list the full 25-chapter table of contents and appendices from the user-provided requirements.

#### Scenario: Reader opens the book entry point

- **WHEN** a reader opens `book/README.md`
- **THEN** they SHALL see the book title, target audience, prerequisites, complete chapter list, appendix list, and staged generation plan

### Requirement: Chapter structure contract

Each complete chapter SHALL include learning objectives, principle explanation, implementation analysis where relevant, reproducible examples where relevant, at least one visualization, summary, and chapter references.

#### Scenario: Complete chapter review

- **WHEN** a complete chapter is reviewed
- **THEN** the reviewer SHALL be able to identify learning objectives, visualization content, summary, and references in that chapter

### Requirement: Narrative depth and integrated examples

Complete chapters SHALL be written as substantive book chapters, not outline-style notes, and example code SHALL be integrated with explanatory prose.

#### Scenario: Example-driven chapter review

- **WHEN** a chapter includes an eBPF example
- **THEN** the chapter SHALL include the code or a complete planned code block, compile/run commands, expected output, and prose explaining the key lines, data structures, helper calls, attachment point, and output path

### Requirement: Standalone example assets

Examples referenced by practical chapters SHALL be materialized as standalone files under `book/examples/<chapter>/` when the corresponding chapter is completed, rather than existing only as inline prose.

#### Scenario: Practical chapter completion

- **WHEN** a practical chapter is marked complete
- **THEN** every runnable example promised by that chapter SHALL have standalone source files, a Makefile or build script, a run script where appropriate, and a README explaining prerequisites and verification status

#### Scenario: Skeleton chapter with planned examples

- **WHEN** a chapter is still a skeleton
- **THEN** its `book/examples/<chapter>/` directory MAY be empty, but the OpenSpec task list SHALL include explicit pending tasks to generate those standalone example assets

### Requirement: Chapter 1 complete sample

The first batch SHALL include a complete Chapter 1 sample in Chinese that covers eBPF history, major use cases, comparison with kernel modules, kernel implementation orientation, an operational orientation example, visualization, summary, and references.

#### Scenario: Style calibration

- **WHEN** future chapters are generated
- **THEN** Chapter 1 SHALL serve as the baseline for voice, structure, depth, and reference style

### Requirement: Chapter 9 deep skeleton

The first batch SHALL include a detailed Chapter 9 skeleton that explicitly reserves coverage for the ebpf.top BPF introductory path: Hello World, kprobe, tracepoint, map, and perf event.

#### Scenario: Chapter 9 requirement audit

- **WHEN** Chapter 9 is audited against the user requirements
- **THEN** the skeleton SHALL mention Hello World, kprobe, tracepoint, map, perf event, compile commands, loader scripts, expected output, and verification notes

### Requirement: Chapter 9 ebpf.top full introductory series coverage

Chapter 9 SHALL cover all 12 articles in the ebpf.top BPF introductory series, including technical introduction, learning path, environment setup, Hello World, PID filtering, container security, ring buffer, file-open tracing, perf_event file-open tracing, return value tracing, tracepoint file-open tracing, and Netfilter with Go experience.

#### Scenario: ebpf.top series audit

- **WHEN** Chapter 9 is audited against the ebpf.top BPF introductory series
- **THEN** the chapter SHALL contain one identifiable section for each of the 12 articles and SHALL map each article to concrete example code, commands, expected output, or verification requirements

### Requirement: Chapter 17 deep skeleton

The first batch SHALL include a detailed Chapter 17 skeleton for a high-performance eBPF firewall prototype, covering XDP/TC selection, conntrack, ACL, NAT, checksum updates, userspace control plane, and benchmarking.

#### Scenario: Chapter 17 requirement audit

- **WHEN** Chapter 17 is audited against the user requirements
- **THEN** the skeleton SHALL mention datapath/control-plane separation, XDP versus TC, conntrack map/state machine, ACL matching, SNAT/DNAT, checksum update, rule management, and pktgen or iperf benchmarking

### Requirement: Source and verification policy

The manuscript SHALL prioritize the user's authoritative reference list and SHALL avoid claiming local execution of eBPF examples unless verification was actually performed in a suitable kernel environment.

#### Scenario: Example cannot be run locally

- **WHEN** an eBPF example requires privileges or kernel features unavailable in the current environment
- **THEN** the manuscript SHALL mark the environment assumptions and provide expected output as illustrative rather than locally verified
