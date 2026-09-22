import struct

data = open(r'C:\l4tools\l4capture\stream_dump.h264', 'rb').read()

# SPS starts at offset4 (after 00 00 00 01)
sps_byte = data[4]
forbidden = (sps_byte >> 7) & 1
nal_ref = (sps_byte >> 5) & 3
nal_type = sps_byte & 0x1F
print(f'SPS NAL header: 0x{sps_byte:02X} (forbidden={forbidden}, ref_idc={nal_ref}, type={nal_type})')

# SPS payload starts at offset5
sps = data[5:19]  # 15 bytes of SPS
print(f'SPS payload ({len(sps)} bytes): {sps.hex()}')

# Parse SPS (simplified)
# profile_idc
profile_idc = sps[0]
constraint_set0 = (sps[1] >> 7) & 1
constraint_set1 = (sps[1] >> 6) & 1
constraint_set2 = (sps[1] >> 5) & 1
constraint_set3 = (sps[1] >> 4) & 1
level_idc = sps[2]

print(f'profile_idc: {profile_idc} ({ "Baseline" if profile_idc==66 else "Main" if profile_idc==77 else "High" if profile_idc==100 else "unknown" })')
print(f'constraint_set0_flag: {constraint_set0}')
print(f'constraint_set1_flag: {constraint_set1}')
print(f'constraint_set2_flag: {constraint_set2}')
print(f'constraint_set3_flag: {constraint_set3}')
print(f'level_idc: {level_idc} ({level_idc//10}.{level_idc%10})')

# PPS
pps = data[23:27]  # 4 bytes after 00 00 00 01 at offset19
print(f'PPS payload ({len(pps)} bytes): {pps.hex()}')

# Check IDR size
idr_start = 27
idr_end = data.find(b'\x00\x00\x00\x01', idr_start + 1)
if idr_end == -1:
    idr_end = len(data)
idr_size = idr_end - idr_start
print(f'IDR slice: {idr_size} bytes')

# SDP-compatible profile-level-id
profile_idc_hex = f'{profile_idc:02X}'
constraint_byte = sps[1]
level_byte = sps[2]
pli = f'{profile_idc_hex}{constraint_byte:02X}{level_byte:02X}'
print(f'SDP profile-level-id: {pli}')
print(f'SDP sprop-parameter-sets needs base64 SPS+PPS')
