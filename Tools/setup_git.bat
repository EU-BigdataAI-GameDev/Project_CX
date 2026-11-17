@echo off
rem 저장소를 처음 클론했을 때 한 번 실행한다.
rem git이 한글 파일 경로를 "\354\..." 같은 이스케이프로 출력하면
rem UnrealBuildTool의 git 작업 세트 판별(adaptive non-unity build)이 예외로 죽어 빌드가 실패한다.
rem quotepath를 끄면 경로가 UTF-8 그대로 출력되어 문제가 사라진다(이 저장소에만 적용).
cd /d "%~dp0.."
git config core.quotepath false
echo core.quotepath = false (UnrealBuildTool 한글 경로 빌드 오류 방지)
