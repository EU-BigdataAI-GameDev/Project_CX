# 깃허브 이슈 / PR 업로드 가이드

Claude Code가 GitHub CLI(`gh`)로 이슈와 PR을 올릴 때 따르는 규칙. 저장소: `EU-BigdataAI-GameDev/Project_CX`.

## 0. 준비

- `gh`는 설치돼 있고 사용자 계정으로 로그인돼 있다(`gh auth status`로 확인). 로그인은 사용자가 직접 한다.
- 현재 세션 PATH에 `gh`가 없으면 `C:\Program Files\GitHub CLI\gh.exe`로 직접 실행한다(PowerShell: `& "C:\Program Files\GitHub CLI\gh.exe" ...`). Claude Code를 다시 열면 `gh`만 쳐도 된다.
- 한국어 본문은 터미널 인코딩 문제가 생길 수 있으므로 **파일로 저장한 뒤 `--body-file`로 올린다**(임시 파일은 scratchpad에, 저장소에 남기지 않는다).
- 이슈와 PR은 팀원에게 보이는 외부 게시다. 올린 뒤 삭제해도 알림은 남는다. 본문에 API 키·토큰·개인 정보를 넣지 않는다.

## 1. 작업 시작 전에 이슈를 올린다

- 새 작업(기능, 버그 수정, 문서, 리팩토링 등)을 **시작하기 전에** 이슈를 작성해 업로드한다. 이 규칙은 사용자가 미리 허용한 것이라, 작업마다 올려도 되는지 따로 묻지 않는다.
- **한 브랜치에 이슈는 하나만** 작성한다. 같은 브랜치에서 성격이 다른 작업이 생기면 브랜치를 나눈다(또는 기존 이슈에 이어서 쓴다). 브랜치 이름과 이슈 제목이 같은 작업을 가리키게 한다.
- 이슈 번호를 작업 브랜치와 PR 문서(`Closes #번호`)에 연결한다.
- 이미 같은 이슈가 있으면 새로 만들지 않고 기존 이슈에 연결한다(`gh issue list --search "키워드"`로 먼저 찾는다).
- 템플릿은 `.github/ISSUE_TEMPLATE/`의 양식(제목 접두어와 라벨)을 따른다.

| 종류 | 제목 접두어 | 라벨 |
|---|---|---|
| 버그 | `[BUG] ` | `bug` (심각하면 `priority:high`) |
| 기능 | `[FEATURE] ` | `feature` 또는 `enhancement` |
| 일반 작업(문서·리팩토링 등) | `[TASK] ` | `task` |
| 아트·에셋 요청 | `[ART] ` | `art`, `asset-request` |

저장소에 실제 있는 라벨은 `gh label list`로 확인한다(예: `bug`, `documentation`, `question`, `feature`, `task`, `priority:high|medium|low`). 없는 라벨은 `gh`가 오류를 내므로 쓰지 않는다.

```powershell
& "C:\Program Files\GitHub CLI\gh.exe" issue create --title "[TASK] 제목" --body-file <본문.md> --label task
```

본문은 짧고 구체적으로: 무엇을 / 왜 / 완료 조건. 버그는 재현 절차, 확인한 것, 추정 원인, 제안을 쓴다.

## 2. 버그 이슈는 중요한 것만, 올리기 전에 묻는다

- **사소한 버그는 이슈를 만들지 않는다**(오타, 한 줄 수정, 바로 고치고 끝나는 것, 취향 차이).
- **중요한 버그**(진행 불가, 크래시, 데이터 손실, 시연·제출에 영향)를 발견하면, 이슈를 작성하기 전에 **사용자에게 먼저 묻는다.** 제목·심각도·한두 줄 요약을 보여 주고 확인을 받은 뒤 올린다.
- 사용자가 "이슈로 만들어 줘"라고 직접 요청한 경우는 묻지 않고 올린다.
- 이슈를 올린 뒤에는 번호와 링크를 사용자에게 알린다.

## 3. PR은 "PR 올려줘"라고 요청받았을 때만

1. **PR 문서 작성:** `.github/PRD/PR_<브랜치 이름의 / 를 - 로>.md`에 작성한다(예: `fix/lkj-camera-occlusion` → `PR_fix-lkj-camera-occlusion.md`).
   - 양식은 `.github/pull_request_template.md`의 섹션을 그대로 유지한다.
   - 첫 줄은 h1 제목이다: `# [TYPE] 간결한 설명`(TYPE = FEAT / FIX / DOCS / REFACTOR 등). **이 줄이 PR 제목이 된다.**
   - 둘째 줄 이후 `- 브랜치: ... → develop`처럼 브랜치 정보를 적는다.
   - 짧고 리뷰어가 보기 좋게 쓴다.
   - **관련 이슈 번호를 반드시 연결한다.** "관련 이슈" 섹션에 `Closes #번호`(이 브랜치의 이슈)를 적는다. 이슈가 없으면 먼저 1절대로 만든 뒤 적는다.
2. **문서를 커밋하고 브랜치를 푸시**한다(PR 문서도 같이 푸시).
3. **PR 업로드:** 문서 내용을 **그대로** 본문에 올린다. 제목은 문서 첫 줄의 h1 제목에서 `# `를 뺀 문자열이다.
   - 본문은 h1 제목 줄을 제외한 나머지를 쓴다(제목이 중복되지 않게).
   - 기본 대상 브랜치는 `develop`이다. 문서에 적은 대상이 다르면 그것을 따른다.

   ```powershell
   $doc = ".github\PRD\PR_<이름>.md"
   $lines = Get-Content $doc -Encoding UTF8
   $title = ($lines[0] -replace '^#\s*', '')
   $lines[1..($lines.Length-1)] | Set-Content <scratchpad>\pr_body.md -Encoding UTF8
   & "C:\Program Files\GitHub CLI\gh.exe" pr create --base develop --head <브랜치> --title $title --body-file <scratchpad>\pr_body.md
   ```
4. **PR을 올렸으면 연결된 이슈를 닫는다**(아래 3-1). PR 번호와 링크, 닫은 이슈를 사용자에게 알린다. 머지는 사용자가 한다(요청 없이 머지하지 않는다).

"PR 올려줘" 요청이 없으면 PR 문서만 만들고 업로드하지 않는다. 문서 작성 요청과 업로드 요청을 구분한다.

### 3-1. 이슈 닫기

- **PR을 올리면** 그 PR이 해결하는 이슈를 닫는다.
- PR 없이 끝나는 작업(문서, 설정 등)이나 **버그가 해결되면** 작업이 끝난 시점에 이슈를 닫는다.
- `Closes #번호`는 기본 브랜치(`main`)에 머지될 때만 자동으로 닫히고, 이 저장소는 `develop`으로 머지하므로 **자동으로 닫히지 않는다.** 그래서 직접 닫는다.
- 닫을 때 해결 내용을 한두 줄 코멘트로 남긴다(PR 링크 포함).

  ```powershell
  & "C:\Program Files\GitHub CLI\gh.exe" issue close <번호> --comment "PR #<PR번호>에서 해결(내용 한 줄)"
  ```
- 해결되지 않았거나 일부만 끝났으면 닫지 않고 남은 일을 코멘트로 적는다.

## 4. 하지 않는 것

- 요청 없이 PR 올리기, 머지, 라벨·담당자 임의 변경
- 끝나지 않은 이슈 닫기, 한 브랜치에 이슈 여러 개 만들기
- 사소한 버그 이슈 남발, 같은 이슈 중복 생성
- 이슈·PR 본문에 키·토큰·개인 정보 넣기
- `gh auth login` 등 인증 대신 하기(사용자가 직접 한다)
