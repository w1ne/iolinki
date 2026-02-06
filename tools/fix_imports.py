import os


def fix_file(filepath):
    print(f"Processing {filepath}...")
    with open(filepath, "r") as f:
        lines = f.readlines()

    shebang = []
    copyright_lines = []
    rest_lines = []

    idx = 0

    # Check for shebang
    if lines and lines[0].startswith("#!"):
        shebang.append(lines[0])
        idx += 1

    # Skip empty lines before copyright
    while idx < len(lines) and not lines[idx].strip():
        # We might drop these empty lines or keep them? Dropping for cleanliness.
        idx += 1

    # Check for Copyright Header
    if idx < len(lines) and lines[idx].strip().startswith('"""'):
        # Only treat as Copyright if it actually contains "Copyright"
        # Peek ahead reasonable amount
        chunk = "".join(lines[idx : idx + 10])
        if "Copyright" in chunk:
            copyright_lines.append(lines[idx])
            idx += 1
            while idx < len(lines):
                copyright_lines.append(lines[idx])
                if lines[idx].strip().startswith('"""'):
                    idx += 1
                    break
                idx += 1
        else:
            print(
                f"  Skipping copyright detection for {filepath}: docstring found but no 'Copyright'"
            )
    else:
        print(
            f"  Skipping {filepath}: No docstring/copyright found at expected position"
        )

    if not copyright_lines:
        return

    # Remaining lines
    rest_lines = lines[idx:]

    import_lines = []
    other_lines = []

    seen_code = False

    for line in rest_lines:
        sline = line.strip()
        if not sline:  # Empty line
            if not seen_code:
                continue  # Skip empty lines before code/imports
            else:
                other_lines.append(line)
        elif sline.startswith("import ") or sline.startswith("from "):
            if not seen_code:
                import_lines.append(line)
            else:
                other_lines.append(line)
        elif sline.startswith("#"):
            # Comments before code are tricky. If it's pure comment, keep with imports?
            # If it's docstring?
            # Let's assume comments belong to next block.
            if not seen_code:
                # If next line is import, this is a comment for import.
                # This is hard to predict.
                # SAFE STRATEGY: Treat everything else as 'other' and stop import scan?
                # Actually, usually imports are contiguous.
                if not import_lines:
                    # No imports seen yet. If this is a comment, maybe legal?
                    # But we want to move imports UP.
                    # If we see a comment, we treat it as code?
                    seen_code = True
                    other_lines.append(line)
                else:
                    # We have imports, now we see a comment.
                    # Could be comment for next import.
                    # Let's peek? No.
                    # Simplified: If we see something that is NOT import/from, we stop collecting global imports.
                    seen_code = True
                    other_lines.append(line)
        else:
            # Code, class, def, docstring
            seen_code = True
            other_lines.append(line)

    # Reconstruct
    final_lines = []
    if shebang:
        final_lines.extend(shebang)

    final_lines.extend(copyright_lines)
    final_lines.append("\n")  # Spacer

    if import_lines:
        final_lines.extend(import_lines)
        final_lines.append("\n")

    final_lines.extend(other_lines)

    # Write back
    with open(filepath, "w") as f:
        f.writelines(final_lines)
    print(f"  Fixed {filepath}")


def main():
    target_dir = "tools/virtual_master"
    for root, dirs, files in os.walk(target_dir):
        for file in files:
            if file.endswith(".py"):
                fix_file(os.path.join(root, file))


if __name__ == "__main__":
    main()
