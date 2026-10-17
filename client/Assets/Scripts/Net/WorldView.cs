using System.Collections.Generic;
using UnityEngine;

namespace Coop.Net
{
    // 스냅샷을 네모로 그린다. 보간도 예측도 없다 —
    // 서버가 준 좌표를 30Hz 그대로 찍는다. 끊겨 보이는 게 정상이고,
    // 그걸 눈으로 본 다음에 보간을 넣는다.
    [RequireComponent(typeof(BattleClient))]
    public class WorldView : MonoBehaviour
    {
        BattleClient client;
        Sprite square;

        // 엔티티 ID -> 화면의 네모.
        readonly Dictionary<uint, SpriteRenderer> views =
            new Dictionary<uint, SpriteRenderer>();

        readonly HashSet<uint> seen = new HashSet<uint>();
        readonly List<uint> gone = new List<uint>();

        void Awake()
        {
            client = GetComponent<BattleClient>();

            // 1x1 흰 점 하나로 전부 그린다. 색과 크기만 바꾼다.
            var tex = new Texture2D(1, 1);
            tex.SetPixel(0, 0, Color.white);
            tex.Apply();
            square = Sprite.Create(tex, new Rect(0, 0, 1, 1),
                                   new Vector2(0.5f, 0.5f), 1f);
        }

        void LateUpdate()
        {
            var ents = client.Entities;
            int n = client.EntityCount;

            seen.Clear();

            for (int i = 0; i < n; i++)
            {
                ref var e = ref ents[i];
                seen.Add(e.Id);

                if (!views.TryGetValue(e.Id, out var sr))
                {
                    var go = new GameObject(e.Type + " " + e.Id);
                    go.transform.SetParent(transform, false);
                    sr = go.AddComponent<SpriteRenderer>();
                    sr.sprite = square;
                    views[e.Id] = sr;
                }

                sr.transform.localPosition = new Vector3(e.X, e.Y, 0f);
                sr.transform.localScale = Vector3.one * SizeOf(e.Type);
                sr.color = ColorOf(e);
            }

            // 이번 스냅샷에 없는 것은 죽었거나 방을 떠난 것이다.
            gone.Clear();
            foreach (var kv in views)
                if (!seen.Contains(kv.Key)) gone.Add(kv.Key);

            foreach (uint id in gone)
            {
                Destroy(views[id].gameObject);
                views.Remove(id);
            }
        }

        static Color ColorOf(in EntityState e)
        {
            switch (e.Type)
            {
                case EntityType.Player:
                    // 쓰러지면 회색. hp_pct 가 부활 진행도로 바뀐다(17c).
                    return e.Life == EntityLife.Alive ? Color.green : Color.gray;
                case EntityType.Monster: return Color.red;
                case EntityType.Boss: return Color.magenta;
                case EntityType.Door: return Color.yellow;
                default: return Color.white;
            }
        }

        static float SizeOf(EntityType t)
        {
            switch (t)
            {
                case EntityType.Boss: return 4f;
                case EntityType.Door: return 10f;   // kDoorRadius 5 의 지름
                default: return 1f;
            }
        }
    }
}