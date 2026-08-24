# משאבים — Dynamic Binary Translation and Optimization

## מקור אמת מקומי

- `/home/noamgrosman/technionCourses/binary_translation` בתוך WSL הוא עותק הקוד התקף והעדכני.
- המראה תחת `School Vault/.../binary_translation` עדיין שימושית למסמכים ולקישורים, אך אינה מקור האמת לפרויקט הסופי.

## Knowledge

- `../ex1/ex1.cpp`
  מקור האמת למימוש תרגיל 1. אין בתיקייה מפרט רשמי נפרד, ולכן הפרק מסמן במפורש מה נלמד מן הקוד עצמו.
- `../ex2/Exercise2.pdf`, `../ex2/SPEC.md`, `../ex2/ex2.cpp`
  דרישות תרגיל 2, ניסוח מקומי שלהן והמימוש בפועל.
- `../ex3/Exercise3.pdf`, `../ex3/src/README.txt`, `../ex3/src/btranslate.cpp`
  דרישות תרגיל 3, תיאור איתור התקלה והמתרגם המתוקן שהוגש.
- `../ex3/src_fixed/README_FIX.txt`, `../ex3/src_fixed/fix.diff`
  הרחבה ניסויית שמתקנת גם תרגום של בינארים סטטיים גדולים; אינה ההגשה המקורית.
- `ex4/Exercise4.pdf`, `ex4/src/README.txt`, `ex4/src/bprofile.cpp` בעותק WSL
  דרישות תרגיל 4, דו״ח המימוש המפורט והקוד שעבר אימות.
- `../EX4_VERDICT_REPORT.md`
  ביקורת ואימות נפרדים של נכונות תרגיל 4 והמדידות שלו.
- `final_project/project-2026.pdf`, `final_project/bprofile-with-gearing.cpp` בעותק WSL
  מפרט הפרויקט והשלד שסופק ליצירת TC ו־TC2.
- `final_project/src/project.cpp`, `final_project/src/project_baseline.cpp`, `final_project/src/README.txt`
  המימוש המותאם, baseline בעל אותם תיקוני נכונות ותיאור ההגשה.
- `final_project/docs/AUDIT.md`, `final_project/docs/data/`, `final_project/scripts/`
  ledger של הביקורת, evidence גולמי ו־scripts לבנייה, goldens, smoke tests ומדידות interleaved.
- [Intel Pin 4.0 User Guide](https://software.intel.com/sites/landingpage/pintool/docs/99633/Pin/doc/html/index.html)
  תיעוד רשמי למודל ה־JIT, callbacks, traces ו־basic blocks. שימושי בעיקר לתרגילים 1–2.
- [Intel Pin — RTN API](https://software.intel.com/sites/landingpage/pintool/docs/98869/Pin/doc/html/group__RTN.html)
  תיעוד רשמי של routines ושל `RTN_ReplaceProbed`; שימושי לתרגילים 3–4 ולפרויקט.
- [Intel XED User Guide](https://intelxed.github.io/ref-manual/)
  תיעוד רשמי של decode/encode והמבנים `xed_decoded_inst_t` ו־`xed_encoder_request_t`.
- [Intel XED — Encoding](https://intelxed.github.io/ref-manual/group__ENC.html)
  פירוט ממשקי הקידוד שעליהם נשענים בניית ה־TC ותיקון ה־displacements.

## Wisdom (Communities)

- צוות הקורס והמתרגל/המרצה.
  מקור ההכרעה כאשר יש פער בין נוסח ה־PDF, קוד השלד והתנהגות Pin בגרסת סביבת הבדיקה.
- בן/בת הזוג לפרויקט.
  מומלץ לבצע code walkthrough הדדי: אחד מסביר את control flow והשני מחפש מצב שבו registers, flags או target משתנים בטעות.

## Gaps

- מפרט תרגיל 1 אינו נמצא בתיקייה; ההסבר שלו מבוסס על `ex1.cpp`, קובצי הפלט והאופן שבו תרגיל 2 מרחיב אותו.
- ה־audit האדברסרי של הפרויקט החל ב־2026-08-24 ועדיין מכיל leads פתוחים; build, goldens ושחזורי הביצועים המתועדים שעברו אינם שקולים ל־verdict סופי על כל lead.
