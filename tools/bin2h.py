import sys
import os

def bin_to_h(bin_path, h_path, var_name):
    if not os.path.exists(bin_path):
        print(f"Error: {bin_path} does not exist")
        return False
        
    print(f"Converting {bin_path} to {h_path}...")
    with open(bin_path, 'rb') as f:
        data = f.read()
        
    os.makedirs(os.path.dirname(h_path), exist_ok=True)
    with open(h_path, 'w') as f:
        f.write('#pragma once\n\n')
        f.write(f'const unsigned char {var_name}[] = {{\n')
        
        # Write hex bytes, 12 per line
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_strs = [f"0x{b:02x}" for b in chunk]
            f.write("  " + ", ".join(hex_strs) + ",\n")
            
        f.write('};\n')
    print("Conversion done.")
    return True

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("Usage: python3 bin2h.py <bin_path> <h_path> <var_name>")
        sys.exit(1)
    bin_to_h(sys.argv[1], sys.argv[2], sys.argv[3])
