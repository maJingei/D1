class ProtoParser():
    def __init__(self, start_id, recv_prefix, send_prefix):
        self.recv_pkt = []   # 수신 패킷 목록
        self.send_pkt = []   # 송신 패킷 목록
        self.total_pkt = []  # 전체 패킷 목록
        self.start_id = start_id
        self.id = start_id
        self.recv_prefix = recv_prefix
        self.send_prefix = send_prefix

    def parse_proto(self, path):
        f = open(path, 'r', encoding='utf-8')
        lines = f.readlines()

        for line in lines:
            if line.startswith('message') == False:
                continue

            # message 키워드 뒤 첫 토큰을 대문자로 변환
            pkt_name = line.split()[1].upper()
            if pkt_name.startswith(self.recv_prefix):
                self.recv_pkt.append(Packet(pkt_name, self.id))
            elif pkt_name.startswith(self.send_prefix):
                self.send_pkt.append(Packet(pkt_name, self.id))
            else:
                continue

            # Handler에서 Enum 값을 한 번에 참조하기 위해 total에도 추가
            self.total_pkt.append(Packet(pkt_name, self.id))
            self.id += 11

        f.close()


class Packet:
    def __init__(self, name, id):
        self.name = name
        self.id = id
