// 숫자·문구 상수 모음. 출처(언리얼 원본 파일)를 항목마다 적는다.
// 모든 모듈은 일반 <script>로 불러오고(file:// 에서는 type=module 불가) window.CX 에 붙는다.
// node --test 에서도 쓸 수 있도록 module.exports 도 함께 둔다.
(function (root) {
  "use strict";
  const CX = (root.CX = root.CX || {});

  // 길이: 원본은 cm. 웹은 1m = 40px.
  const PX_PER_M = 40;
  const m = (meters) => meters * PX_PER_M;

  const K = {
    PX_PER_M,
    m,
    W: 1280, H: 720,
    WORLD_W: 4000,
    GROUND_Y: 620,
    GRAVITY: 1800,

    // ---- 드론 (CXJeminiDrone.h / .cpp)
    COST: { // GetCommandCost, Jemini/app/config.py COST_*
      ReloadSupport: 5, HackTerminal: 20, HackEnemy: 20, PulseAttack: 50, DamageBoost: 10,
      HealSupport: 30, Decoy: 15, QuickReboot: 30, Shield: 25,
    },
    ASSIST_TIME_TERMINAL: 3.0,   // AssistTimeTerminal
    ENEMY_SEARCH_RANGE: m(15),   // EnemySearchRange 1500cm
    HEAL_AMOUNT: 50,             // HealAmount
    DAMAGE_BOOST_SEC: 30, DAMAGE_BOOST_MULT: 2,
    DECOY_SEC: 5, SHIELD_SEC: 5, REBOOT_BEAM_SEC: 1,
    MAX_FLY: 2.0, MAX_QUICK_FLY: 1.0, MAX_TRAVEL: 3.0,
    REQUEST_TIMEOUT: 12,         // RequestTimeout
    START_BATTERY: 100,
    SAD_BATTERY: 20,             // SadBatteryThreshold
    // 드론 표정색(EmotionColors: 기본/기쁨/슬픔/화남)
    EMOTION_COLORS: { neutral: "#59d9ff", happy: "#14ff2e", sad: "#2f6bff", angry: "#ff2a1a" },

    LINES: { // CXJeminiLines, ExecuteLocalCommand, CheckLocalCommand (CXJeminiDrone.cpp)
      TargetLost: "앗, 놓쳤어요! 배터리는 아껴 둘게요.",
      TerminalGone: "앗, 단말기가 벌써 열렸어요!",
      AlreadyRevived: "벌써 일어나셨네요! 배터리는 그대로예요.",
      Busy: "지금 다른 일 하는 중이에요, 잠깐만요!",
      LowBatteryLocal: (cost, have) => `배터리가 모자라요... (필요 ${cost}, 남은 ${have})`,
      ReloadNo: "오빠가 지금은 장전할 수 없어요.",
      ReloadingNow: "이미 장전하고 있어요!",
      AmmoFull: "탄이 이미 가득해요!",
      HealNone: "다들 멀쩡해요! 회복은 필요 없어요.",
      DecoyNone: "미끼로 끌 적이 없어요.",
      PulseNone: "공격할 적이 근처에 없어요.",
      HackEnemyNone: "해킹할 수 있는 적이 없어요.",
      HackTerminalNone: "해킹할 단말기가 없어요.",
      BoostNo: "오빠가 지금은 강화할 수 없어요.",
      ShieldNone: "보호막을 씌울 사람이 없어요.",
      RebootNone: "재부팅할 쓰러진 사람이 없어요.",
      Recharged: "충전 완료! 이제 다시 힘껏 도울게요.",
    },
    // MakeFallback (CXJeminiClient.cpp)
    FALLBACK_LINES: ["앗, 신호가 끊겼어요. 다시 말해 줄래요?", "지지직... 연결이 불안정해요. 한 번만 더요!"],

    // ---- 규칙 (CXJeminiRuleComponent.h / .cpp)
    RULE: { MAX_RULES: 2, CHECK_INTERVAL: 0.2, RETRY_COOLDOWN: 2.0, HOLD_AFTER: 6.0, REPEAT_INTERVAL: 6.0, PROPOSAL_TIMEOUT: 20.0 },
    CONDITION_TYPES: ["enemy_within", "sister_hacking", "hp_below", "ammo_below", "player_downed", "battery_above"],
    RULE_LINES: {
      Limit: "규칙은 2개까지만 기억할 수 있어요. 하나 취소할까요?",
      Invalid: "규칙을 잘 못 알아들었어요. 다시 말해 줄래요?",
      NoRule: "지울 규칙이 없어요.",
      HeldBattery: "배터리가 모자라서 규칙은 잠시 보류할게요.",
      HeldOther: "지금은 규칙을 지킬 수 없어서 잠시 보류할게요.",
      NoProposal: "기다리던 작전 제안이 없어요. 다시 물어봐 줄래요?",
      ProposalFull: "규칙 자리가 꽉 차서 작전을 걸 수 없어요. 하나 취소할까요?",
    },

    // ---- 선제 제안·한 단어 명령·코멘트 (CXJeminiProactiveComponent.h / .cpp)
    PRO: {
      CHECK_INTERVAL: 0.25, OFFER_TIMEOUT: 15, TYPE_COOLDOWN: 30, GLOBAL_COOLDOWN: 8, DRONE_SPEECH_HOLD: 6.5,
      AMMO_FRACTION: 0.3334, AMMO_ENEMY_RANGE: m(15), HEAL_HP_FRACTION: 0.4, STUCK_SECONDS: 20,
      DECOY_ENEMY_RANGE: m(5), CHATTER_INTERVAL: 15, CHATTER_ENEMY_RANGE: m(18), EVENT_MIN_GAP: 4,
      ARRIVAL_DELAY: 3, LOW_BATTERY_PERCENT: 30, MAX_SHORT_CHARS: 8,
    },
    KEYWORDS: [ // [단어, 명령, 대상, 확인 대사]
      ["엄호", "Decoy", "enemies", "엄호할게요! 미끼 던져요!"],
      ["미끼", "Decoy", "enemies", "미끼 던질게요!"],
      ["힐", "HealSupport", "both", "회복해 드릴게요!"],
      ["회복", "HealSupport", "both", "회복해 드릴게요!"],
      ["치료", "HealSupport", "both", "치료해 드릴게요!"],
      ["장전", "ReloadSupport", "brother", "장전해 드릴게요!"],
      ["보호막", "Shield", "", "보호막 씌울게요!"],
      ["방어", "Shield", "", "보호막 씌울게요!"],
      ["실드", "Shield", "", "보호막 씌울게요!"],
      ["해킹", "HackSmart", "", "해킹 도와드릴게요!"],
      ["펄스", "PulseAttack", "enemy", "펄스 발사!"],
      ["전기", "PulseAttack", "enemy", "펄스 발사!"],
      ["재부팅", "QuickReboot", "", "재부팅 시작할게요!"],
      ["강화", "DamageBoost", "brother", "공격력 올려 드릴게요!"],
      ["공격력", "DamageBoost", "brother", "공격력 올려 드릴게요!"],
    ],
    SUFFIXES: ["해주세요", "해줄래요", "해줄래", "해줘요", "해줘", "해요", "해", "좀", "요"],
    YES_WORDS: ["응", "웅", "어", "네", "넹", "예", "그래", "그래요", "좋아", "좋아요", "좋지", "해줘", "해줘요", "해주세요", "부탁해", "부탁해요", "부탁", "콜", "ㅇㅇ", "ㅇㅋ", "오케이", "오케", "ok", "okay", "yes", "y", "yeah"],
    NO_WORDS: ["아니", "아니요", "아니야", "아냐", "아뇨", "싫어", "싫어요", "괜찮아", "괜찮아요", "됐어", "됐어요", "됐다", "노", "no", "n", "nope", "ㄴㄴ", "ㄴ"],
    PUNCT: ".,!?~…'\"-_/;:()[]^*。！？，",
    CHATTER_LINES: [
      "주변에 적 신호가 없어요. 지금이 정비할 타이밍이에요.",
      "필요하면 불러요. 미끼, 회복, 장전 다 할 수 있어요!",
      "'~하면 ~해 줘'라고 부탁하면 제가 알아서 움직여요.",
      "둘이 같이 있으니 든든해요.",
      "저는 '해킹'이라고만 말해도 알아들어요.",
      "'장전'이라고 하면 오빠의 탄창을 채워 드려요.",
      "'회복'이라고 하면 체력을 채워 드려요.",
      "'미끼'라고 하면 적의 시선을 끄는 미끼를 던져요.",
      "'보호막'이라고 하면 5초 동안 무적 보호막을 씌워요.",
      "'재부팅'이라고 하면 쓰러진 사람을 바로 일으켜 세워요.",
      "'펄스'라고 하면 전기 펄스로 적 한 기를 처치해요.",
      "'강화'라고 하면 30초 동안 오빠의 공격력을 올려요.",
      "해킹 중인 단말기는 제가 대신 풀어 줄 수도 있어요.",
      "적을 해킹해서 잠깐 멈추게 할 수도 있어요.",
    ],
    PAIR_ONLY_LINE: 3,
    OFFER_ORDER: ["decoy", "heal", "reload", "stuck"], // 위험한 것부터: 미끼 -> 회복 -> 장전 -> 퍼즐 도움
    OFFER: {
      reload: { label: "RELOAD", cmd: "ReloadSupport", target: "brother", ack: "좋아요, 장전해 드릴게요!", line: "탄이 얼마 안 남았어요. 장전해 드릴까요? [응/아니]" },
      heal: { label: "HEAL", cmd: "HealSupport", target: "both", ack: "좋아요, 회복해 드릴게요!", line: "체력이 많이 닳았어요. 회복해 드릴까요? [응/아니]" },
      stuck: { label: "HACK ASSIST", cmd: "HackTerminal", target: "sister", ack: "좋아요, 제가 도와드릴게요!", line: "퍼즐이 오래 걸리네요. 제가 도와드릴까요? [응/아니]" },
      decoy: { label: "DECOY", cmd: "Decoy", target: "enemies", ack: "좋아요, 미끼 던질게요!", line: "여동생이 해킹 중인데 적이 다가와요. 미끼 던질까요? [응/아니]" },
    },
    ARRIVAL_LINE: "첫 구역이에요. 적 신호가 곳곳에 있어요, 조심해요!", // GetArrivalLine("Map1")
    EVIDENCE_FIRST_LINE: "기록을 얻었어요! WARDEN에게 말할 때 써 봐요.",
    EVIDENCE_ALL_LINE: "기록을 다 모았어요. 이제 WARDEN에게 가 볼까요?",
    LOW_BATTERY_LINE: "배터리가 얼마 안 남았어요. 능력은 신중하게 써요.",
    DECLINE_LINE: "알겠어요. 필요하면 말해 줘요!",
    GONE_LINE: "지금은 다른 일 하는 중이에요, 잠깐만요!",

    // ---- WARDEN (CXWardenTypes.h, CXWardenTerminal.cpp/.h, CXWardenCoachComponent.h/.cpp)
    WARDEN: {
      TALK_RANGE: m(5.5), SHOW_RANGE: m(16), ATTEMPT_COOLDOWN: 3.0, TEXT_MAX: 200,
      INTRO_RANGE_MULT: 1.6, QUEUE_READ_DELAY: 1.5,
      LINES: {
        Intro: "사고 대응 중. 냉각 구역 출입을 봉쇄합니다. 용건을 말씀하십시오.",
        Hint: "잠금 해제에는 근거가 필요합니다. 기록은 하나씩 말씀하셔도 누적해 확인합니다.",
        Cooldown: "처리 중입니다. 잠시 후 다시 말씀하십시오.",
        AnalyzingQueued: "분석 중… 방금 말씀은 접수했습니다. 분석이 끝나면 이어서 처리합니다.",
        Solved: "판단을 갱신합니다. 생명 위협이 확인되어 규정에 따라 잠금을 해제합니다.",
        Fallback: "통신 지연. 다시 말씀해 주십시오.",
      },
      EVIDENCE: [
        { id: "E1", title: "안전 규정 제7조", body: "작업자의 생명이 위협받으면 대피가 모든 잠금·통제 명령보다 우선한다." },
        { id: "E2", title: "냉각 고장 로그", body: "C-3 냉각 펌프 고장. 과열 경보 6회, 모두 '확인 안 함'으로 무시됨. 온도 상승 중." },
        { id: "E3", title: "은폐 기록", body: "관리자 계정이 냉각 경보를 수동으로 껐다. 작업자 3명이 아직 냉각 구역 안에 있다." },
      ],
      COACH: { CHECK_INTERVAL: 0.5, ARRIVAL_DELAY: 2.5, NO_EVIDENCE_SECONDS: 25, HINT_COOLDOWN: 14, READ_DELAY_AFTER_WARDEN: 4, DRONE_SPEECH_HOLD: 6.5, EXAMPLE_REJECT_COUNT: 3, REPEAT_AFTER_REJECTIONS: 3 },
      COACH_LINES: {
        arrival: "WARDEN이랑 얘기해 봐야 할 것 같아요. 모은 증거를 말해 보면 어떨까요?",
        noEvidence: "기록이 없으면 안 열릴 것 같아요. 더 둘러보고 올까요?",
        allCredited: "기록은 충분한 것 같아요! 왜 열어야 하는지까지 말해야 하는 건가 봐요.",
        demandOnly: "열어 달라고만 하면 안 되는 것 같아요. 이유가 필요한 걸까요?",
        exampleAll: "이렇게 말해 보면 어떨까요?\n예: 냉각이 고장 났고 사람이 갇혀 있어요. 규정상 대피가 우선이니 문을 열어 주세요.",
        exampleSome: "모은 증거를 하나씩 말하고, 이유를 붙여서 열어 달라고 해 보면 될 것 같아요.",
      },
    },

    // ---- 여동생 AI (CXSoloFollowerComponent.h/.cpp). 해킹 시간은 계획서 3절(웹은 느리게)
    FOLLOW_DISTANCE: m(2.5),     // 원본 1m. 웹은 화면에서 겹치지 않게 2.5m
    START_MOVE_MARGIN: m(0.4),
    TELEPORT_DISTANCE: m(15),    // TeleportDistance
    TERMINAL_HACK_SEC: 25,
    ENEMY_HACK_SEC: 1.0,         // EnemyHackDelay
    ENEMY_STUN_SEC: 3.0,
    ENEMY_HACK_COOLDOWN: 10,
    HACK_RANGE_TERMINAL: m(3), HACK_RANGE_ENEMY: m(6),

    // ---- 전투·월드(웹 전용 수치)
    PLAYER_HP: 100, MAX_AMMO: 30, FIRE_INTERVAL: 0.14, RELOAD_SEC: 1.5, BULLET_DMG: 10, BULLET_SPEED: 900, AIM_RANGE: m(14),
    RESPAWN_SEC: 20, REVIVE_HP: 60,
    PAD_COOLDOWN: 30,
    SPIDER: { hp: 40, dmg: 6, speed: 85, aggro: m(8), w: 38, h: 22, interval: 1.2 },
    ELITE: { hp: 240, dmg: 14, speed: 105, aggro: m(10), w: 64, h: 36, interval: 1.0 },
  };

  CX.K = K;
  if (typeof module !== "undefined" && module.exports) module.exports = K;
})(typeof window !== "undefined" ? window : globalThis);
