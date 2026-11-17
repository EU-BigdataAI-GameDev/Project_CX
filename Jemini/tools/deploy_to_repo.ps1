# 제미니 서버 파일을 "배포용 저장소(clone)"에 복사하고 커밋까지 한다. push는 직접 한다(로그인은 사용자가 처리).
# 배포용 저장소는 Render가 연결하는 GitHub 저장소(예: jemini-server)다. 허깅페이스 Space 저장소에도 같은 방식으로 쓸 수 있다.
#
# 사용법(PowerShell):
#   git clone https://github.com/<계정>/jemini-server C:\Users\<사용자>\source\repos\jemini-server
#   cd C:\Users\<사용자>\source\repos\Project_CX
#   .\Jemini\tools\deploy_to_repo.ps1 -TargetDir C:\Users\<사용자>\source\repos\jemini-server
#   cd C:\Users\<사용자>\source\repos\jemini-server ; git push
#
# 복사하는 것: app/ prompts/ Dockerfile requirements-space.txt README.md .dockerignore
# 복사하지 않는 것: .env(API 키), .venv, models, logs, tests, tools, demo — 키는 Render 환경변수에 넣는다.
param(
    [Parameter(Mandatory = $true)][string]$TargetDir,
    [string]$Message = "deploy jemini server"
)

$ErrorActionPreference = "Stop"
$src = Split-Path -Parent $PSScriptRoot   # Jemini/

if (-not (Test-Path (Join-Path $TargetDir ".git"))) {
    throw "$TargetDir 는 git 저장소가 아니다. 먼저 git clone 으로 배포용 저장소를 받아 둘 것."
}

# 이전 배포의 코드 폴더는 지우고 새로 복사한다(삭제된 파일이 남지 않게)
foreach ($dir in @("app", "prompts")) {
    $dst = Join-Path $TargetDir $dir
    if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
    Copy-Item (Join-Path $src $dir) $dst -Recurse
    Get-ChildItem $dst -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force
}
foreach ($file in @("Dockerfile", "requirements-space.txt", "README.md", ".dockerignore")) {
    Copy-Item (Join-Path $src $file) (Join-Path $TargetDir $file) -Force
}

# 비밀 파일이 섞이지 않았는지 확인(이름만 본다. 내용은 출력하지 않는다)
$secrets = Get-ChildItem $TargetDir -Recurse -Force -File | Where-Object {
    $_.FullName -notmatch "\\\.git\\" -and ($_.Name -eq ".env" -or $_.Name -like "*.env")
}
if ($secrets) {
    throw "비밀 파일로 보이는 것이 있다: $($secrets.FullName -join ', ') — 지우고 다시 실행할 것."
}

git -C $TargetDir add -A
git -C $TargetDir commit -m $Message
Write-Host ""
Write-Host "복사와 커밋이 끝났다. 이제 직접 push 한다:  cd $TargetDir ; git push"
