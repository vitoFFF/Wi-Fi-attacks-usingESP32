import os
import gzip
import binascii
import sys

def convert_html_to_header(html_file):
    if not os.path.exists(html_file):
        print(f"File {html_file} not found!")
        return

    name = os.path.splitext(os.path.basename(html_file))[0]
    output_filename = f"page_{name}.h"
    
    # Read HTML
    with open(html_file, 'rb') as f:
        html_data = f.read()
    
    # Gzip HTML
    gzipped_data = gzip.compress(html_data, compresslevel=9)
    
    # Generate Header
    guard = f"PAGE_{name.upper()}_H"
    
    with open(output_filename, 'w') as f:
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write("// This file was generated using convert_html_to_header.py\n")
        
        # Hex dump
        var_name = f"page_{name}"
        f.write(f"unsigned char {var_name}[] = {{\n  ")
        
        hex_data = [f"0x{b:02X}" for b in gzipped_data]
        for i, h in enumerate(hex_data):
            f.write(h)
            if i < len(hex_data) - 1:
                f.write(", " if (i + 1) % 12 != 0 else ",\n  ")
        
        f.write("\n};\n")
        f.write(f"unsigned int {var_name}_len = {len(gzipped_data)};\n\n")
        f.write(f"#endif\n")

    print(f"Generated {output_filename}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python convert_html_to_header.py <filename.html>")
    else:
        convert_html_to_header(sys.argv[1])
