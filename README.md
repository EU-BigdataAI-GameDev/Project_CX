## 🌿 브랜치 전략

### 브랜치 구조

| 브랜치 | 용도 |
| --- | --- |
| `main` | 최종 릴리즈 전용. 직접 push 절대 금지 |
| `develop` | 개발 통합 브랜치. PR을 통해서만 병합 |
| `feat/닉네임-개발기능` | 기능 단위 개발. develop에서 분기, develop으로 PR|
||최신 버전 develop 브랜치로 최신화한 후 분기 만들 것|

### 브랜치 네이밍 규칙

반드시 영어 소문자 + 케밥케이스로 작성.

| 타입 | 형식 | 예시 |
| --- | --- | --- |
| 기능 개발 | `feat/기능명` | `feat/player-movement` |
| 버그 수정 | `fix/버그명` | `fix/jump-glitch` |
| 긴급 수정 | `hotfix/내용` | `hotfix/crash-on-start` |
| 리팩토링 | `refactor/내용` | `refactor/input-system` |

### PR 머지 규칙

- `feature` → `develop` : 승인 **1명** 이상 필요
- `develop` → `main` : 승인 **2명** 이상 필요
- PR 올리기 전 반드시 PR 템플릿 내용 채울 것 (`.github/PULL_REQUEST_TEMPLATE.md`)
- 리뷰 코멘트 모두 resolve 후 머지 가능
- force push 금지

### 작업 흐름

1. `develop` 에서 `feature` 브랜치 생성
2. 기능 개발 후 커밋
3. `develop` 으로 PR 생성 (템플릿 작성)
4. 팀원 리뷰 및 승인
5. 머지 후 `feature` 브랜치 삭제

---

## 🔗 GitHub 이슈 관리 규칙

- **feat/** — 새로운 기능
- **fix/** — 버그 수정
- **art/** — 아트 에셋 관련
- **docs/** — 문서 작업
- **refactor/** — 코드 개선

> 이슈 생성 시 반드시 마일스톤 + 담당자 + 라벨 지정할 것.
> 

---

## 📐 코드 컨벤션

### 네이밍 규칙

| 구분 | 규칙 | 예시 |
| --- | --- | --- |
| 클래스 | PascalCase | `PlayerController` |
| 메서드 | PascalCase | `MovePlayer()` |
| 변수 | camelCase | `moveSpeed` |
| 상수 | UPPER_SNAKE | `MAX_HEALTH` |
| 프라이빗 필드 | _camelCase | `_isGrounded` |

### 커밋 메시지 규칙

```
[feat] 플레이어 점프 기능 추가
[fix] 적 충돌 버그 수정
[art] 메인 캐릭터 스프라이트 추가
[docs] README 업데이트
```

### PR 규칙

- main 브랜치 직접 push 금지
- PR은 최소 1명 리뷰 후 머지
- PR 제목에 관련 이슈 번호 명시 (`#이슈번호`)
