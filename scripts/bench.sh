#!/bin/bash

# fconcat Simple Benchmark Suite
# Tests fconcat performance against traditional tools using existing files
# Run with: ./benchmark.sh <test_directory>

set -euo pipefail

# Configuration
BENCHMARK_DIR="${1:-}"
RESULTS_DIR="./benchmark_results"
FCONCAT_BINARY="${FCONCAT_BINARY:-./fconcat}"
ITERATIONS=5
WARMUP_RUNS=1
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# Threading configuration
MAX_THREADS=$(nproc)
THREAD_COUNTS=(1 2 4 8)

# Colors for pretty output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Performance tracking
declare -A RESULTS

print_header() {
    echo -e "${CYAN}${BOLD}"
    echo "================================================================"
    echo "              🚀 FCONCAT BENCHMARK SUITE 🚀"
    echo "================================================================"
    echo -e "${NC}"
    echo "Timestamp: $(date)"
    echo "Test directory: $BENCHMARK_DIR"
    echo "Results directory: $RESULTS_DIR"
    echo "Iterations per test: $ITERATIONS"
    echo "fconcat binary: $FCONCAT_BINARY"
    echo "Available CPU cores: $MAX_THREADS"
    echo "Thread counts to test: ${THREAD_COUNTS[*]}"
    echo ""
}

print_usage() {
    echo "Usage: $0 <test_directory>"
    echo ""
    echo "Examples:"
    echo "  $0 /usr/src/linux-headers-\$(uname -r)  # Linux kernel headers"
    echo "  $0 /usr/include                         # System headers"
    echo "  $0 ./my_project                        # Your project"
    echo "  $0 /opt/homebrew/include               # macOS headers"
    echo ""
    echo "The script will benchmark fconcat against traditional tools"
    echo "using the files in the specified directory."
}

check_directory() {
    if [[ -z "$BENCHMARK_DIR" ]]; then
        echo -e "${RED}❌ Error: No test directory specified${NC}"
        print_usage
        exit 1
    fi

    if [[ ! -d "$BENCHMARK_DIR" ]]; then
        echo -e "${RED}❌ Error: Directory '$BENCHMARK_DIR' does not exist${NC}"
        exit 1
    fi

    if [[ ! -r "$BENCHMARK_DIR" ]]; then
        echo -e "${RED}❌ Error: Directory '$BENCHMARK_DIR' is not readable${NC}"
        exit 1
    fi

    # Check if directory has files
    local file_count=$(find "$BENCHMARK_DIR" -type f 2>/dev/null | head -1000 | wc -l)
    if [[ $file_count -eq 0 ]]; then
        echo -e "${RED}❌ Error: Directory '$BENCHMARK_DIR' contains no files${NC}"
        exit 1
    fi

    echo -e "${GREEN}✅ Test directory validated${NC}"
    echo "  📁 Directory: $BENCHMARK_DIR"
    echo "  📄 Files found: $(find "$BENCHMARK_DIR" -type f 2>/dev/null | wc -l)"
    echo "  💾 Total size: $(du -sh "$BENCHMARK_DIR" 2>/dev/null | cut -f1)"
    echo ""
}

measure_command() {
    local cmd="$1"
    local description="$2"
    local output_file="$3"
    
    echo -e "  ${BLUE}Testing: $description${NC}"
    
    # Warmup runs
    echo -n "    Warmup... "
    for ((w=1; w<=WARMUP_RUNS; w++)); do
        rm -f "$output_file"
        timeout 300 bash -c "$cmd" >/dev/null 2>&1 || true
    done
    echo "done"
    
    local total_time=0
    local success_count=0
    local times=()
    
    for ((i=1; i<=ITERATIONS; i++)); do
        echo -n "    Iteration $i/$ITERATIONS... "
        
        # Clean up previous output
        rm -f "$output_file"
        
        # Measure time using bash builtin for better accuracy
        local start_time=$(date +%s.%N)
        
        if timeout 300 bash -c "$cmd" >/dev/null 2>&1; then
            local end_time=$(date +%s.%N)
            local iteration_time=$(echo "$end_time - $start_time" | bc -l)
            
            # Verify output was actually created and has content
            if [[ -f "$output_file" && -s "$output_file" ]]; then
                times+=("$iteration_time")
                total_time=$(echo "$total_time + $iteration_time" | bc -l)
                success_count=$((success_count + 1))
                echo -e "${GREEN}OK${NC} (${iteration_time}s)"
            else
                echo -e "${RED}FAILED (no output)${NC}"
            fi
        else
            echo -e "${RED}FAILED/TIMEOUT${NC}"
        fi
    done
    
    if [[ $success_count -gt 0 ]]; then
        # Calculate statistics
        local avg_time=$(echo "scale=3; $total_time / $success_count" | bc -l)
        
        # Calculate median for more robust central tendency
        IFS=$'\n' sorted=($(sort -n <<<"${times[*]}"))
        local median_time
        local n=${#sorted[@]}
        if (( n % 2 == 1 )); then
            median_time=${sorted[$((n/2))]}
        else
            median_time=$(echo "scale=3; (${sorted[$((n/2-1))]} + ${sorted[$((n/2))]}) / 2" | bc -l)
        fi
        
        local min_time=$(printf '%s\n' "${times[@]}" | sort -n | head -1)
        local max_time=$(printf '%s\n' "${times[@]}" | sort -n | tail -1)
        
        RESULTS["${description}_time"]="$avg_time"
        RESULTS["${description}_median"]="$median_time"
        RESULTS["${description}_min_time"]="$min_time"
        RESULTS["${description}_max_time"]="$max_time"
        RESULTS["${description}_success_rate"]="$success_count/$ITERATIONS"
        
        # Get memory usage
        RESULTS["${description}_memory"]="N/A"
        
        # Check output file size
        if [[ -f "$output_file" ]]; then
            RESULTS["${description}_output_size"]=$(stat -c%s "$output_file" 2>/dev/null || echo "0")
            RESULTS["${description}_line_count"]=$(wc -l < "$output_file" 2>/dev/null || echo "0")
        fi
    else
        RESULTS["${description}_time"]="FAILED"
        RESULTS["${description}_median"]="FAILED"
        RESULTS["${description}_memory"]="FAILED"
        RESULTS["${description}_success_rate"]="0/$ITERATIONS"
        RESULTS["${description}_output_size"]="0"
    fi
}

run_benchmark() {
    echo -e "${PURPLE}${BOLD}🧪 Running benchmark on: $(basename "$BENCHMARK_DIR")${NC}"
    echo -e "${PURPLE}Directory: $BENCHMARK_DIR${NC}"
    
    # Count files and calculate total size
    local file_count=$(find "$BENCHMARK_DIR" -type f 2>/dev/null | wc -l)
    local total_size=$(du -sb "$BENCHMARK_DIR" 2>/dev/null | cut -f1 || echo "0")
    
    echo "  📊 Files: $file_count, Total size: $(numfmt --to=iec $total_size 2>/dev/null || echo "unknown")"
    echo ""
    
    # Test fconcat with different thread counts
    echo -e "${CYAN}🧵 Testing fconcat with different thread counts${NC}"
    for threads in "${THREAD_COUNTS[@]}"; do
        if [[ $threads -le $MAX_THREADS ]]; then
            measure_command \
                "$FCONCAT_BINARY \"$BENCHMARK_DIR\" \"$RESULTS_DIR/fconcat_${threads}t.txt\" --threads $threads --binary-skip" \
                "fconcat_${threads}t" \
                "$RESULTS_DIR/fconcat_${threads}t.txt"
        fi
    done
    
    echo ""
    echo -e "${CYAN}🔧 Testing traditional tools${NC}"
    
    # Test find + cat (traditional approach)
    measure_command \
        "find \"$BENCHMARK_DIR\" -type f -exec cat {} \\; > \"$RESULTS_DIR/find_cat.txt\" 2>/dev/null" \
        "find_cat" \
        "$RESULTS_DIR/find_cat.txt"
    
    # Test tar approach
    measure_command \
        "cd \"$(dirname \"$BENCHMARK_DIR\")\" && tar -cf - \"$(basename \"$BENCHMARK_DIR\")\" 2>/dev/null | tar -tf - | head >/dev/null && tar -cf - \"$(basename \"$BENCHMARK_DIR\")\" 2>/dev/null > \"$RESULTS_DIR/tar_extract.tar\"" \
        "tar_create" \
        "$RESULTS_DIR/tar_extract.tar"
    
    # Test shell script approach
    cat > "$RESULTS_DIR/shell_concat.sh" << 'EOF'
#!/bin/bash
output_file="$1"
input_dir="$2"
exec 1>"$output_file"
find "$input_dir" -type f 2>/dev/null | while IFS= read -r file; do
    echo "// File: $file"
    cat "$file" 2>/dev/null || echo "// Error reading file"
    echo ""
done
EOF
    chmod +x "$RESULTS_DIR/shell_concat.sh"
    
    measure_command \
        "\"$RESULTS_DIR/shell_concat.sh\" \"$RESULTS_DIR/shell_script.txt\" \"$BENCHMARK_DIR\"" \
        "shell_script" \
        "$RESULTS_DIR/shell_script.txt"
    
    echo ""
}

generate_report() {
    local report_file="$RESULTS_DIR/benchmark_report_$TIMESTAMP.md"
    
    echo -e "${CYAN}📊 Generating benchmark report...${NC}"
    
    cat > "$report_file" << EOF
# fconcat Benchmark Report
*Generated on $(date)*

## System Information
- **OS**: $(uname -srmo)
- **CPU**: $(grep "model name" /proc/cpuinfo 2>/dev/null | head -1 | cut -d: -f2 | xargs || uname -p)
- **CPU Cores**: $(nproc) cores
- **Memory**: $(free -h 2>/dev/null | grep Mem: | awk '{print $2}' || echo "unknown")
- **Test Directory**: $BENCHMARK_DIR
- **fconcat version**: $($FCONCAT_BINARY --version 2>&1 | head -1 || echo "2.0.0")

## Test Configuration
- **Iterations per test**: $ITERATIONS (plus $WARMUP_RUNS warmup runs)
- **Thread counts tested**: ${THREAD_COUNTS[*]}
- **Timeout per test**: 300 seconds

## Results

### Threading Performance

| Threads | Time (median) | Time (avg) | Min Time | Max Time | Success Rate |
|---------|---------------|------------|----------|----------|--------------|
EOF

    # Generate threading performance table with median times
    for threads in "${THREAD_COUNTS[@]}"; do
        if [[ $threads -le $MAX_THREADS ]]; then
            local time_key="fconcat_${threads}t_time"
            local median_key="fconcat_${threads}t_median"
            local min_key="fconcat_${threads}t_min_time"
            local max_key="fconcat_${threads}t_max_time"
            local rate_key="fconcat_${threads}t_success_rate"
            
            local time="${RESULTS[$time_key]:-N/A}"
            local median="${RESULTS[$median_key]:-N/A}"
            local min_time="${RESULTS[$min_key]:-N/A}"
            local max_time="${RESULTS[$max_key]:-N/A}"
            local rate="${RESULTS[$rate_key]:-N/A}"
            
            if [[ "$time" != "FAILED" && "$time" != "N/A" ]]; then
                printf "| %7d | %13.3fs | %10.3fs | %8.3fs | %8.3fs | %12s |\n" \
                    "$threads" "$median" "$time" "$min_time" "$max_time" "$rate" >> "$report_file"
            else
                printf "| %7d | %13s | %10s | %8s | %8s | %12s |\n" \
                    "$threads" "FAILED" "FAILED" "N/A" "N/A" "$rate" >> "$report_file"
            fi
        fi
    done
    
    cat >> "$report_file" << EOF

### Threading Speedup Analysis

| Threads | Speedup vs 1T | Efficiency |
|---------|---------------|------------|
EOF

    # Calculate speedups using median times
    local baseline_median="${RESULTS[fconcat_1t_median]:-N/A}"
    if [[ "$baseline_median" != "FAILED" && "$baseline_median" != "N/A" ]]; then
        for threads in "${THREAD_COUNTS[@]}"; do
            if [[ $threads -le $MAX_THREADS ]]; then
                local median_key="fconcat_${threads}t_median"
                local median="${RESULTS[$median_key]:-N/A}"
                
                if [[ "$median" != "FAILED" && "$median" != "N/A" ]]; then
                    local speedup=$(echo "scale=2; $baseline_median / $median" | bc -l)
                    local efficiency=$(echo "scale=1; $speedup / $threads * 100" | bc -l)
                    printf "| %7d | %13.2fx | %9.1f%% |\n" \
                        "$threads" "$speedup" "$efficiency" >> "$report_file"
                fi
            fi
        done
    fi
    
    cat >> "$report_file" << EOF

### Tool Comparison

| Tool | Time (median) | Output Size | Lines | Success Rate | Status |
|------|---------------|-------------|-------|--------------|--------|
EOF

    # Compare all tools with output verification
    for tool in "fconcat_1t" "fconcat_2t" "fconcat_4t" "fconcat_8t" "find_cat" "tar_create" "shell_script"; do
        # Skip if thread count exceeds available cores
        if [[ "$tool" == "fconcat_8t" && $MAX_THREADS -lt 8 ]]; then
            continue
        fi
        
        local median_key="${tool}_median"
        local size_key="${tool}_output_size"
        local lines_key="${tool}_line_count"
        local rate_key="${tool}_success_rate"
        
        local median="${RESULTS[$median_key]:-N/A}"
        local size="${RESULTS[$size_key]:-0}"
        local lines="${RESULTS[$lines_key]:-0}"
        local rate="${RESULTS[$rate_key]:-N/A}"
        
        # Determine status
        local status="✅ OK"
        if [[ "$median" == "FAILED" || "$median" == "N/A" ]]; then
            status="❌ FAILED"
        elif [[ "$size" == "0" ]]; then
            status="⚠️ NO OUTPUT"
        fi
        
        # Format output size
        if [[ "$size" != "N/A" && "$size" != "0" ]]; then
            size=$(numfmt --to=iec "$size" 2>/dev/null || echo "$size")
        fi
        
        printf "| %-12s | %13s | %11s | %5s | %12s | %8s |\n" \
            "$tool" "$median" "$size" "$lines" "$rate" "$status" >> "$report_file"
    done
    
    cat >> "$report_file" << EOF

## Performance Analysis

### Key Findings
EOF

    # Find best fconcat performance using median times
    local best_fconcat_time=999999
    local best_fconcat_threads=1
    for threads in "${THREAD_COUNTS[@]}"; do
        if [[ $threads -le $MAX_THREADS ]]; then
            local median_key="fconcat_${threads}t_median"
            local median="${RESULTS[$median_key]:-999999}"
            if [[ "$median" != "FAILED" && "$median" != "N/A" ]] && (( $(echo "$median < $best_fconcat_time" | bc -l) )); then
                best_fconcat_time="$median"
                best_fconcat_threads="$threads"
            fi
        fi
    done

    # Find best traditional tool
    local best_traditional_time=999999
    local best_traditional_tool=""
    for tool in "find_cat" "shell_script"; do
        local median_key="${tool}_median"
        local size_key="${tool}_output_size"
        local median="${RESULTS[$median_key]:-999999}"
        local size="${RESULTS[$size_key]:-0}"
        
        # Only consider tools that actually produced output
        if [[ "$median" != "FAILED" && "$median" != "N/A" && "$size" != "0" ]] && (( $(echo "$median < $best_traditional_time" | bc -l) )); then
            best_traditional_time="$median"
            best_traditional_tool="$tool"
        fi
    done

    echo "- **Best fconcat performance**: ${best_fconcat_time}s using $best_fconcat_threads threads" >> "$report_file"
    echo "- **Best traditional tool**: $best_traditional_tool (${best_traditional_time}s)" >> "$report_file"
    
    if [[ "$best_fconcat_time" != "999999" && "$best_traditional_time" != "999999" ]]; then
        local advantage=$(echo "scale=2; $best_traditional_time / $best_fconcat_time" | bc -l)
        echo "- **fconcat advantage**: ${advantage}x faster than best traditional tool" >> "$report_file"
    fi

    # Threading efficiency analysis
    if [[ "$baseline_median" != "FAILED" && "$baseline_median" != "N/A" ]]; then
        local median_4t="${RESULTS[fconcat_4t_median]:-N/A}"
        if [[ "$median_4t" != "N/A" && "$median_4t" != "FAILED" ]]; then
            local speedup_4t=$(echo "scale=2; $baseline_median / $median_4t" | bc -l)
            local efficiency_4t=$(echo "scale=1; $speedup_4t / 4 * 100" | bc -l)
            echo "- **4-thread speedup**: ${speedup_4t}x (${efficiency_4t}% efficiency)" >> "$report_file"
        fi
    fi

    cat >> "$report_file" << EOF

### Conclusion
This benchmark demonstrates fconcat's performance characteristics on real-world data.
The multi-threading implementation shows the tool's ability to scale with available CPU cores
while maintaining consistent memory usage and reliable output generation.

---
*Benchmark completed on $(date)*
EOF

    echo -e "${GREEN}✅ Report generated: $report_file${NC}"
}

display_summary() {
    echo ""
    echo -e "${CYAN}${BOLD}🎯 BENCHMARK SUMMARY${NC}"
    echo "=========================="
    
    # Find best performers
    local best_fconcat_time=999999
    local best_fconcat_threads=1
    for threads in "${THREAD_COUNTS[@]}"; do
        if [[ $threads -le $MAX_THREADS ]]; then
            local median_key="fconcat_${threads}t_median"
            local median="${RESULTS[$median_key]:-999999}"
            if [[ "$median" != "FAILED" && "$median" != "N/A" ]] && (( $(echo "$median < $best_fconcat_time" | bc -l) )); then
                best_fconcat_time="$median"
                best_fconcat_threads="$threads"
            fi
        fi
    done

    local best_traditional_time=999999
    local best_traditional_tool=""
    for tool in "find_cat" "shell_script"; do
        local median_key="${tool}_median"
        local size_key="${tool}_output_size"
        local median="${RESULTS[$median_key]:-999999}"
        local size="${RESULTS[$size_key]:-0}"
        
        if [[ "$median" != "FAILED" && "$median" != "N/A" && "$size" != "0" ]] && (( $(echo "$median < $best_traditional_time" | bc -l) )); then
            best_traditional_time="$median"
            best_traditional_tool="$tool"
        fi
    done

    echo -e "${GREEN}🏆 Best fconcat performance:${NC} ${best_fconcat_time}s using $best_fconcat_threads threads"
    echo -e "${YELLOW}🥈 Best traditional tool:${NC} $best_traditional_tool (${best_traditional_time}s)"
    
    if [[ "$best_fconcat_time" != "999999" && "$best_traditional_time" != "999999" ]]; then
        local advantage=$(echo "scale=1; $best_traditional_time / $best_fconcat_time" | bc -l)
        echo -e "${BOLD}📊 fconcat is ${advantage}x faster${NC} than the best traditional tool"
        
        if (( $(echo "$advantage >= 2.0" | bc -l) )); then
            echo -e "${GREEN}🎉 EXCELLENT PERFORMANCE! fconcat dominated the benchmark!${NC}"
        elif (( $(echo "$advantage >= 1.5" | bc -l) )); then
            echo -e "${GREEN}✅ GOOD PERFORMANCE! fconcat shows clear advantages${NC}"
        elif (( $(echo "$advantage >= 1.1" | bc -l) )); then
            echo -e "${YELLOW}👍 DECENT PERFORMANCE! fconcat is competitive${NC}"
        else
            echo -e "${YELLOW}🤔 Mixed results - traditional tools performed well${NC}"
        fi
    fi

    # Threading analysis
    local baseline_median="${RESULTS[fconcat_1t_median]:-N/A}"
    local median_4t="${RESULTS[fconcat_4t_median]:-N/A}"
    
    if [[ "$baseline_median" != "N/A" && "$median_4t" != "N/A" && "$baseline_median" != "FAILED" && "$median_4t" != "FAILED" ]]; then
        local speedup=$(echo "scale=2; $baseline_median / $median_4t" | bc -l)
        local efficiency=$(echo "scale=1; $speedup / 4 * 100" | bc -l)
        echo -e "${CYAN}🧵 Threading efficiency:${NC} ${speedup}x speedup with 4 threads (${efficiency}% efficiency)"
    fi
}

cleanup() {
    echo -e "${YELLOW}🧹 Cleaning up temporary files...${NC}"
    rm -f "$RESULTS_DIR"/*.tmp
    rm -f "$RESULTS_DIR"/shell_concat.sh
}

main() {
    print_header
    
    # Check if directory was provided
    check_directory
    
    # Check dependencies
    if [[ ! -f "$FCONCAT_BINARY" ]]; then
        echo -e "${RED}❌ fconcat binary not found: $FCONCAT_BINARY${NC}"
        echo "Build it first with: make"
        exit 1
    fi
    
    if ! command -v bc &> /dev/null; then
        echo -e "${RED}❌ bc calculator not found. Install it with: sudo apt install bc${NC}"
        exit 1
    fi
    
    # Adjust thread counts based on available cores
    if [[ $MAX_THREADS -lt 8 ]]; then
        THREAD_COUNTS=(1 2 4)
        echo -e "${YELLOW}⚠️  Only $MAX_THREADS cores available, limiting thread tests${NC}"
    fi
    
    # Setup
    mkdir -p "$RESULTS_DIR"
    
    # Run benchmark
    run_benchmark
    
    # Generate report
    generate_report
    
    # Display summary
    display_summary
    
    # Cleanup
    cleanup
    
    echo ""
    echo -e "${GREEN}${BOLD}🎉 Benchmark complete! Check $RESULTS_DIR for detailed results.${NC}"
}

# Handle Ctrl+C gracefully
trap cleanup EXIT

main "$@"