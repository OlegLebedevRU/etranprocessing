import sys

data = open(r'C:\l4tools\l4capture\stream_dump.h264', 'rb').read()
print(f'File size: {len(data)} bytes')

pos = 0
nal_count = 0
nal_names = {1: 'P-slice', 5: 'IDR', 7: 'SPS', 8: 'PPS', 6: 'SEI'}

while pos < len(data) - 4:
    if data[pos:pos+4] == b'\x00\x00\x00\x01':
        nal_header = data[pos+4]
        nal_type = nal_header & 0x1F
        nal_ref = (nal_header >> 5) & 3
        next_pos = data.find(b'\x00\x00\x00\x01', pos + 4)
        if next_pos == -1:
            next_pos = len(data)
        nal_size = next_pos - pos - 4
        name = nal_names.get(nal_type, f'type{nal_type}')
        print(f'  NAL @{pos}: {name} type={nal_type} ref_idc={nal_ref} size={nal_size}')
        nal_count += 1
        if nal_count > 50:
            print('  ... (truncated)')
            break
        pos = next_pos
    else:
        pos += 1

print(f'Total NALs: {nal_count}')
