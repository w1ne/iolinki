#!/usr/bin/env python3
import os
import sys
import subprocess
import re
import json

def get_git_log(from_tag, to_tag):
    """Get git log messages between two tags."""
    cmd = ["git", "log", f"{from_tag}..{to_tag}", "--pretty=format:%s"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        return result.stdout.split('\n')
    except subprocess.CalledProcessError:
        return []

def categorize_commits(log_lines):
    """Categorize commits based on conventional commits."""
    categories = {
        "Features": [],
        "Bug Fixes": [],
        "Documentation": [],
        "Refactoring": [],
        "Testing": [],
        "Maintenance": []
    }
    
    for line in log_lines:
        line = line.strip()
        if not line: continue
        
        if line.startswith("feat"):
            categories["Features"].append(line)
        elif line.startswith("fix"):
            categories["Bug Fixes"].append(line)
        elif line.startswith("docs"):
            categories["Documentation"].append(line)
        elif line.startswith("refactor") or line.startswith("style"):
            categories["Refactoring"].append(line)
        elif line.startswith("test"):
            categories["Testing"].append(line)
        else:
            categories["Maintenance"].append(line)
            
    return categories

def parse_test_results(build_dir):
    """Parse CTest results."""
    # Assuming ctest --output-on-failure was run and we can get a summary
    # Or parsing a JUnit XML if available. simpler approach: use ctest -N json
    try:
        cmd = ["ctest", "--show-only=json-v1"]
        result = subprocess.run(cmd, cwd=build_dir, capture_output=True, text=True, check=True)
        data = json.loads(result.stdout)
        total = len(data.get("tests", []))
        # Note: This only gets available tests, not results. 
        # For results we need LastTest.log or similar, OR passing results in args.
        # simpler: we'll trust the caller to pass pass/fail counts or parse output text
        pass
    except Exception as e:
        print(f"Error parsing tests: {e}")
        return 0, 0
    return 0, 0

def get_coverage_summary(coverage_file):
    """Parse lcov summary."""
    try:
        with open(coverage_file, 'r') as f:
            content = f.read()
            # extract lines/functions coverage
            # LCOV output usually has "lines......: 85.5% (123 of 145)" in stdout of --summary
            # If reading info file, it's harder. Let's rely on lcov --summary output captured file
            pass
    except:
        return "N/A"
    return "N/A"

def generate_markdown(version, date, categories, test_stats, coverage_stats):
    lines = []
    lines.append(f"# Release {version}")
    lines.append(f"**Date:** {date}")
    lines.append("")
    
    lines.append("## 📊 Quality Report")
    lines.append("| Metric | Status |")
    lines.append("| :--- | :--- |")
    lines.append(f"| **Tests** | ✅ {test_stats['passed']} Passed / {test_stats['total']} Total |")
    lines.append(f"| **Coverage** | 📈 {coverage_stats} |")
    lines.append("")
    
    lines.append("## 🚀 New Features")
    if categories["Features"]:
        for item in categories["Features"]:
            lines.append(f"- {item}")
    else:
        lines.append("- _No major features in this release_")
    lines.append("")

    lines.append("## 🐛 Bug Fixes")
    if categories["Bug Fixes"]:
        for item in categories["Bug Fixes"]:
            lines.append(f"- {item}")
    else:
        lines.append("- _No bug fixes in this release_")
    lines.append("")
    
    lines.append("<details>")
    lines.append("<summary>Other Changes (Docs, Refactor, Maint)</summary>")
    lines.append("")
    for cat in ["Documentation", "Refactoring", "Testing", "Maintenance"]:
        if categories[cat]:
            lines.append(f"### {cat}")
            for item in categories[cat]:
                lines.append(f"- {item}")
            lines.append("")
    lines.append("</details>")
    
    return "\n".join(lines)

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print("Usage: generate_release_notes.py <version> <prev_tag> <test_summary_file> <coverage_summary_file>")
        sys.exit(1)
        
    version = sys.argv[1]
    prev_tag = sys.argv[2]
    test_json = sys.argv[3]
    cov_file = sys.argv[4]
    
    # 1. log
    log = get_git_log(prev_tag, "HEAD")
    cats = categorize_commits(log)
    
    # 2. tests
    t_passed = 0
    t_total = 0
    try:
        with open(test_json, 'r') as f:
            # Assuming we save a simple json: {"total": X, "passed": Y, "failed": Z}
            d = json.load(f)
            t_passed = d.get('passed', 0)
            t_total = d.get('total', 0)
    except:
        pass
        
    # 3. coverage
    cov_str = "N/A"
    try:
        with open(cov_file, 'r') as f:
            cov_str = f.read().strip()
    except:
        pass

    import datetime
    date_str = datetime.date.today().strftime("%Y-%m-%d")

    md = generate_markdown(version, date_str, cats, {"passed": t_passed, "total": t_total}, cov_str)
    print(md)
