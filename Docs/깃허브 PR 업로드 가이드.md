# 깃허브 브랜치 / PR 업로드 가이드

Claude Code가 브랜치를 만들고 GitHub CLI(`gh`)로 PR을 올릴 때 따르는 규칙. 저장소: `EU-BigdataAI-GameDev/Project_CX`.

## 0. 준비

- `gh`는 설치돼 있고 사용자 계정으로 로그인돼 있다(`gh auth status`로 확인). 로그인은 사용자가 직접 한다.
- 현재 세션 PATH에 `gh`가 없으면 `C:\Program Files\GitHub CLI\gh.exe`로 직접 실행한다(PowerShell: `& "C:\Program Files\GitHub CLI\gh.exe" ...`). Claude Code를 다시 열면 `gh`만 쳐도 된다.
- 한국어 본문은 터미널 인코딩 문제가 생길 수 있으므로 **파일로 저장한 뒤 `--body-file`로 올린다**(임시 파일은 scratchpad에, 저장소에 남기지 않는다).
- PR은 팀원에게 보이는 외부 게시다. 본문에 API 키·토큰·개인 정보를 넣지 않는다.

## 1. 작업 시작 전에 브랜치를 만든다

- 새 작업(기능, 버그 수정, 문서, 리팩토링 등)을 **시작하기 전에** 최신 `develop`에서 새 브랜치를 만든다(`git checkout develop`, `git pull --ff-only`, `git checkout -b <브랜치>`). 이 규칙은 사용자가 미리 허용한 것이라, 작업마다 따로 묻지 않는다.
- **한 브랜치에는 한 가지 작업만 담는다.** 성격이 다른 작업이 생기면 브랜치를 나눈다.
- 이름은 `<타입>/<작성자>-<설명>` 형식이다. 예: `feat/lkj-...`, `fix/lkj-...`, `docs/lkj-...` (TYPE = feat / fix / docs / refactor 등).
- 이미 다른 작업용 브랜치에서 작업 중이면 그 위에서 이어 쓰지 말고 `develop`에서 새로 판다. 아직 커밋하지 않은 변경이 있으면 새 브랜치로 그대로 가져간다.
- 작업이 끝나면 커밋하고 푸시한다(사용자가 요청할 때).

## 2. PR은 "PR 올려줘"라고 요청받았을 때만

1. **PR 문서 작성:** `.github/PRD/PR_<브랜치 이름의 / 를 - 로>.md`에 작성한다(예: `fix/lkj-camera-occlusion` → `PR_fix-lkj-camera-occlusion.md`).
   - 양식은 `.github/pull_request_template.md`의 섹션을 그대로 유지한다.
   - 첫 줄은 h1 제목이다: `# [TYPE] 간결한 설명`(TYPE = FEAT / FIX / DOCS / REFACTOR 등). **이 줄이 PR 제목이 된다.**
   - 둘째 줄 이후 `- 브랜치: ... → develop`처럼 브랜치 정보를 적는다.
   - 짧고 리뷰어가 보기 좋게 쓴다.
2. **문서를 커밋하고 브랜치를 푸시**한다(PR 문서도 같이 푸시).
3. **PR 업로드:** 문서 내용을 **그대로** 본문에 올린다. 제목은 문서 첫 줄의 h1 제목에서 `# `를 뺀 문자열이다.
   - 본문은 h1 제목 줄을 제외한 나머지를 쓴다(제목이 중복되지 않게).
   - 기본 대상 브랜치는 `develop`이다. 문서에 적은 대상이 다르면 그것을 따른다.

   ```powershell
   $doc = ".github\PRD\PR_<이름>.md"
   $lines = Get-Content $doc -Encoding UTF8
   $title = ($lines[0] -replace '^#\s*', '') -replace '"', '\"'   # 제목의 큰따옴표는 \"로 바꿔 전달
   $lines[1..($lines.Length-1)] | Set-Content <scratchpad>\pr_body.md -Encoding UTF8
   & "C:\Program Files\GitHub CLI\gh.exe" pr create --base develop --head <브랜치> --title $title --body-file <scratchpad>\pr_body.md
   ```
   - **제목에 큰따옴표(`"`)가 있으면** Windows PowerShell 5.1이 `gh` 인자를 쪼개 `unknown argument ... please quote all values that have spaces` 오류가 난다. 위 예시처럼 `"`를 `\"`로 바꿔 전달한다(제목에 큰따옴표를 안 쓰면 가장 간단하다).
   - `gh`의 `✓ Created ...` 같은 **성공 메시지가 PowerShell에서 빨간 `NativeCommandError`처럼 보일 수 있다.** `✓`가 있으면 성공이다(종료 코드나 `gh pr view`로 확인).
4. PR 번호와 링크를 사용자에게 알린다. 머지는 사용자가 한다(요청 없이 머지하지 않는다).

"PR 올려줘" 요청이 없으면 PR 문서만 만들고 업로드하지 않는다. 문서 작성 요청과 업로드 요청을 구분한다.

## 3. 하지 않는 것

- 요청 없이 PR 올리기, 머지, 라벨·담당자 임의 변경
- 한 브랜치에 여러 작업 섞기, `develop`이나 `main`에 직접 커밋
- PR 본문에 키·토큰·개인 정보 넣기
- `gh auth login` 등 인증 대신 하기(사용자가 직접 한다)
