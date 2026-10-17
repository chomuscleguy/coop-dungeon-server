using System;
using System.Net;
using System.Net.Sockets;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Coop.Net
{
    public class BattleClient : MonoBehaviour
    {
        // 배틀 서버에 UDP 로 붙는다. 지금은 입장까지만 — 그리지 않는다.
        [SerializeField] private string host = "127.0.0.1";
        [SerializeField] private int port = 7778;

        // 로비가 발급하는 16자 입장권. 비워두면 토큰 없이 보낸다
        // (서버에 BATTLE_ALLOW_ANONYMOUS=1 이 있어야 받아준다).
        [SerializeField] private string token = "";

        private UdpClient udp;
        private ushort nextSequence = 1;
        private bool connected;

        private readonly byte[] sendBuffer = new byte[1400];

        private float handshakeTimer;
        private float statTimer;
        private readonly int[] recvCount = new int[8];

        // 한 스냅샷에 들어올 수 있는 최대 엔티티 수.
        // (1400 - 11 헤더 - 1 개수) / 11 = 126
        public const int MaxEntities = 126;

        public EntityState[] Entities { get; } = new EntityState[MaxEntities];
        public int EntityCount { get; private set; }

        // 서버에게 "여기까지 받았다"를 알려주는 재료.
        // battle_server 의 Connection 과 같은 규칙이다 — 비트 0 이
        // (remoteSequence - 1), 창은 32칸.
        private ushort remoteSequence;
        private uint receivedBits;
        private bool hasReceivedAny;

        private float heartbeatTimer;

        // 서버 틱과 같은 30Hz 로 보낸다. 더 자주 보내면 서버가 버리고,
        // 더 드물게 보내면 250ms(kInputHoldMs) 안에 못 닿아 멈춘다.
        private const float InputInterval = 1f / 30f;
        private float inputTimer;

        private void Start()
        {
            // 에디터 창이 뒤로 가면 유니티가 Update 를 멈춘다.
            // 그러면 하트비트가 끊겨서 서버가 우리를 죽은 걸로 본다.
            Application.runInBackground = true;

            udp = new UdpClient();

            // UDP 에 연결이란 게 없다. Connect 는 소켓이 상대 주소를
            // 기억하게 할 뿐이고, 덕분에 Send 에 주소를 안 적어도 된다.
            udp.Connect(host, port);

            Debug.Log($"[net] {host}:{port} 핸드셰이크 시작");
        }

        private void Update()
        {
            if (!connected)
            {
                handshakeTimer -= Time.deltaTime;
                if (handshakeTimer <= 0f)
                {
                    SendHandshake();
                    handshakeTimer = 0.5f;   // 유실되면 다시 보낸다
                }
            }

            if (connected)
            {
                inputTimer -= Time.deltaTime;
                if (inputTimer <= 0f)
                {
                    SendInput();

                    // 빼기가 아니라 더하기다. 프레임이 밀려도 평균 30Hz 를
                    // 지킨다 — 서버의 expires_at 과 같은 이유다.
                    inputTimer += InputInterval;
                    if (inputTimer < 0f) inputTimer = 0f;   // 너무 밀리면 포기
                }
            }

            Poll();
            LogStats();
        }

        // 보내는 모든 패킷이 ack 를 싣고 간다. 따로 ack 패킷을 만들지
        // 않는 이유다 — 어차피 하트비트든 입력이든 나가는 길이 있다.
        private void Send(PacketType type, int payloadSize = 0)
        {
            var h = new PacketHeader
            {
                Sequence = nextSequence++,
                Ack = remoteSequence,
                AckBits = receivedBits,
                Type = type,
            };
            Protocol.WriteHeader(sendBuffer, h);
            udp.Send(sendBuffer, Protocol.HeaderSize + payloadSize);
        }

        private void SendHandshake()
        {
            int payload = 0;
            if (token.Length == Protocol.TokenSize)
            {
                // 16진수 ASCII 라 한 글자가 정확히 한 바이트다.
                for (int i = 0; i < Protocol.TokenSize; i++)
                    sendBuffer[Protocol.HeaderSize + i] = (byte)token[i];
                payload = Protocol.TokenSize;
            }
            Send(PacketType.Handshake, payload);
        }

        private void SendInput()
        {
            int x = 0, y = 0;

            var kb = Keyboard.current;
            if (kb != null)
            {
                if (kb.aKey.isPressed || kb.leftArrowKey.isPressed) x -= 1;
                if (kb.dKey.isPressed || kb.rightArrowKey.isPressed) x += 1;
                if (kb.sKey.isPressed || kb.downArrowKey.isPressed) y -= 1;
                if (kb.wKey.isPressed || kb.upArrowKey.isPressed) y += 1;
            }

            // -1/0/1 을 -127~127 로 보낸다. 서버가 127 로 나눠 -1~1 로 읽고,
            // 대각선(길이 1.41)은 서버가 정규화한다. 클라가 미리 나누면
            // 두 곳에서 같은 계산을 하게 되고, 언젠가 한쪽만 바뀐다.
            sendBuffer[Protocol.HeaderSize + 0] = (byte)(sbyte)(x * 127);
            sendBuffer[Protocol.HeaderSize + 1] = (byte)(sbyte)(y * 127);

            Send(PacketType.Input, Protocol.InputPayloadSize);
        }

        // 받은 번호를 ack / ack_bits 재료로 접어 넣는다.
        private void OnPacketReceived(ushort seq)
        {
            if (!hasReceivedAny)
            {
                remoteSequence = seq;
                receivedBits = 0;
                hasReceivedAny = true;
                return;
            }

            if (Protocol.SequenceGreaterThan(seq, remoteSequence))
            {
                // 더 최신이 왔다. 비트를 밀고, 옛 remoteSequence 자리를 세운다.
                ushort shift = (ushort)(seq - remoteSequence);   // wraparound 포함
                if (shift < 32)
                    receivedBits = (receivedBits << shift) | (1u << (shift - 1));
                else if (shift == 32)
                    receivedBits = 1u << 31;        // 창 끝에 겨우 걸침
                else
                    receivedBits = 0;               // 32개 넘게 건너뜀

                remoteSequence = seq;
            }
            else if (seq != remoteSequence)
            {
                // 늦게 도착한 옛 패킷. 해당 비트만 세운다.
                ushort diff = (ushort)(remoteSequence - seq);
                if (diff <= 32) receivedBits |= 1u << (diff - 1);
            }
            // seq == remoteSequence 는 중복 수신. 할 일 없음.
        }

        private void Poll()
        {
            // Available > 0 일 때만 읽으면 Receive 가 막히지 않는다.
            // 스레드를 만들지 않고 게임 루프 안에서 처리하는 방법이다.
            while (udp.Available > 0)
            {
                IPEndPoint from = null;
                byte[] data = udp.Receive(ref from);

                if (!Protocol.ReadHeader(data, data.Length, out var h))
                    continue;   // 우리 패킷이 아니다

                OnPacketReceived(h.Sequence);

                int t = (int)h.Type;
                if (t < recvCount.Length) recvCount[t]++;

                // 늦게 도착한 옛 스냅샷은 버린다. OnPacketReceived 가 이미
                // 최신 번호를 갱신했으므로, 같으면 이게 그 최신이다.
                // 안 거르면 화면이 과거로 한 프레임 되돌아간다.
                if (h.Type == PacketType.Snapshot && h.Sequence == remoteSequence)
                    EntityCount = Protocol.ReadSnapshot(data, data.Length, Entities);

                if (h.Type == PacketType.HandshakeAck && !connected)
                {
                    connected = true;
                    Debug.Log($"[net] 입장 성공 (서버 seq {h.Sequence})");
                }
            }
        }

        private void LogStats()
        {
            statTimer += Time.deltaTime;
            if (statTimer < 1f) return;
            statTimer -= 1f;

            Debug.Log($"[net] ack={recvCount[(int)PacketType.HandshakeAck]} " +
                      $"snap={recvCount[(int)PacketType.Snapshot]} " +
                      $"event={recvCount[(int)PacketType.Event]} " +
                      $"room={recvCount[(int)PacketType.RoomState]}");

            Array.Clear(recvCount, 0, recvCount.Length);
        }


        private void OnDestroy()
        {
            // 플레이 모드를 나가도 소켓이 남으면 포트를 붙잡는다.
            udp?.Close();
        }
    }
}