using UnityEngine;

namespace Coop.Net 
{
    public enum PacketType : byte
    {
        Invalid = 0,
        Handshake = 1,       // 클라 -> 서버: 입장 요청
        HandshakeAck = 2,    // 서버 -> 클라: 받아줬다
        Heartbeat = 3,       // 양방향: 살아있음
        Input = 4,           // 클라 -> 서버: 이동 입력
        Snapshot = 5,        // 서버 -> 클라: 월드 상태
        Event = 6,           // 서버 -> 클라: 한 번뿐인 사건
        RoomState = 7,       // 서버 -> 클라: 방 진행 상황
    }

    public struct PacketHeader
    {
        public ushort Sequence;
        public ushort Ack;
        public uint AckBits;
        public PacketType Type;
    }

    public enum EntityType : byte
    {
        Player = 0,
        Monster = 1,
        Door = 2,
        Boss = 3,
    }

    public enum EntityLife : byte
    {
        Alive = 0,
        Down = 1,     // 쓰러짐. 파티원이 일으킬 수 있다
        Dead = 2,     // 못 일어남 (그 판 한정)
    }

    public struct EntityState
    {
        public uint Id;
        public EntityType Type;
        public float X;
        public float Y;
        public byte HpPct;
        public EntityLife Life;
    }

    public static class Protocol
    {
        public const ushort ProtocolId = 0xB47;
        public const int HeaderSize = 11;   // 2+2+2+4+1
        public const int TokenSize = 16;

        // 빅엔디안으로 직접 찍는다.
        // BitConverter 는 CPU 순서를 따라가서 인텔에서는 거꾸로 나간다 —
        // 서버가 리눅스 x86 이라 지금은 같지만, "같은 CPU니까 괜찮다"는
        // 모바일 빌드에서 깨진다.
        public static void WriteU8(byte[] b, int at, byte v) => b[at] = v;

        public static void WriteU16(byte[] b, int at, ushort v)
        {
            b[at] = (byte)(v >> 8);
            b[at + 1] = (byte)v;
        }

        public static void WriteU32(byte[] b, int at, uint v)
        {
            b[at] = (byte)(v >> 24);
            b[at + 1] = (byte)(v >> 16);
            b[at + 2] = (byte)(v >> 8);
            b[at + 3] = (byte)v;
        }

        public static byte ReadU8(byte[] b, int at) => b[at];

        public static ushort ReadU16(byte[] b, int at)
            => (ushort)((b[at] << 8) | b[at + 1]);

        public static uint ReadU32(byte[] b, int at)
            => ((uint)b[at] << 24) | ((uint)b[at + 1] << 16)
             | ((uint)b[at + 2] << 8) | b[at + 3];

        public static void WriteHeader(byte[] b, in PacketHeader h)
        {
            WriteU16(b, 0, ProtocolId);
            WriteU16(b, 2, h.Sequence);
            WriteU16(b, 4, h.Ack);
            WriteU32(b, 6, h.AckBits);
            WriteU8(b, 10, (byte)h.Type);
        }

        // 크기가 모자라거나 표식이 다르면 false. 포트 스캐너의 패킷이
        // 섞여 들어와도 여기서 걸러진다.
        public static bool ReadHeader(byte[] b, int size, out PacketHeader h)
        {
            h = default;
            if (size < HeaderSize) return false;
            if (ReadU16(b, 0) != ProtocolId) return false;

            h.Sequence = ReadU16(b, 2);
            h.Ack = ReadU16(b, 4);
            h.AckBits = ReadU32(b, 6);
            h.Type = (PacketType)ReadU8(b, 10);
            return true;
        }

        // 시퀀스는 ushort 라 65535 다음이 0 이다. 단순 비교로는 어느 쪽이
        // 최신인지 알 수 없어서, 차이가 절반(32768) 이내인 쪽을 최신으로 본다.
        // 30Hz 에서 32768 패킷이면 18분이라, 그 사이에 올 패킷은 없다.
        public static bool SequenceGreaterThan(ushort s1, ushort s2)
            => ((s1 > s2) && (s1 - s2 <= 32768))
            || ((s1 < s2) && (s2 - s1 > 32768));

        public const int EntityStateSize = 11;   // 4+1+2+2+1+1
        public const int InputPayloadSize = 2;

        // 좌표를 int16 으로 담을 때의 배율. 맵이 -50~50 이라
        // 100배 해도 -5000~5000 이고 정밀도는 0.01 유닛이다.
        public const float PositionScale = 100.0f;

        // 스냅샷 본문: [개수 1바이트][엔티티 11바이트] * 개수
        // into 를 미리 받는다. 30Hz 로 배열을 새로 만들면 GC 가 돈다.
        public static int ReadSnapshot(byte[] b, int size, EntityState[] into)
        {
            int off = HeaderSize;
            if (size < off + 1) return 0;

            int count = ReadU8(b, off);
            off += 1;

            if (count > into.Length) count = into.Length;

            int n = 0;
            for (int i = 0; i < count; i++)
            {
                if (off + EntityStateSize > size) break;   // 잘린 패킷

                // int16 을 부호 있는 값으로 되돌린다. C# 은 ushort -> short
                // 변환이 2의 보수 그대로라 캐스팅 한 번이면 된다.
                short xi = (short)ReadU16(b, off + 5);
                short yi = (short)ReadU16(b, off + 7);

                into[n].Id = ReadU32(b, off + 0);
                into[n].Type = (EntityType)ReadU8(b, off + 4);
                into[n].X = xi / PositionScale;
                into[n].Y = yi / PositionScale;
                into[n].HpPct = ReadU8(b, off + 9);
                into[n].Life = (EntityLife)ReadU8(b, off + 10);

                off += EntityStateSize;
                n++;
            }
            return n;
        }
    }
}