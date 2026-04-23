class ProtoParser:
    def __init__(self, start_id, recv_prefix, send_prefix):
        self.recv_pkt = []
        self.send_pkt = []
        self.total_pkt = []
        self.start_id = start_id
        self.id = start_id
        self.recv_prefix = recv_prefix
        self.send_prefix = send_prefix

    def parse_proto(self, path):
        with open(path, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        for line in lines:
            if not line.startswith('message'):
                continue

            pkt_name = line.split()[1]
            upper_name = pkt_name.upper()

            if upper_name.startswith(self.recv_prefix):
                self.recv_pkt.append(Packet(pkt_name, self.id))
            elif upper_name.startswith(self.send_prefix):
                self.send_pkt.append(Packet(pkt_name, self.id))
            else:
                continue

            self.total_pkt.append(Packet(pkt_name, self.id))
            self.id += 1


class Packet:
    def __init__(self, name, id):
        self.name = name
        self.id = id