// 서버 주소와 웹 전용 토큰. 이 파일만 고치면 된다.
// serverUrl: 로컬 서버 시험은 http://127.0.0.1:8000, 배포 서버는 Render 주소(Docs/Jemini/서버_배포_가이드.md 참고).
// token: 웹 전용 토큰(서버 환경변수 WEB_TOKEN과 같은 값)을 여기에. 비워 두면 토큰 없이 요청한다.
window.JEMINI_CONFIG = {
  serverUrl: "http://127.0.0.1:8000",
  token: ""
};
