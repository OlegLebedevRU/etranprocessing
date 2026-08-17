INSERT INTO certificate_pins (pin, terminal_id, status) VALUES ('773773', 1, 'pending') ON CONFLICT (pin) DO NOTHING;
