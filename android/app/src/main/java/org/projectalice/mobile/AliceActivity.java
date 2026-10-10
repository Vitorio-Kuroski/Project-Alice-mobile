package org.projectalice.mobile;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.widget.Toast;

/**
 * NativeActivity com o minimo que o C++ nao consegue fazer sozinho:
 * pedir acesso aos arquivos, abrir o seletor de pastas do Android e mostrar
 * mensagens. Todo o resto (janela, EGL, toque, jogo) continua na libAlice.so.
 *
 * Os metodos publicos sao chamados pelo C++ (entry_point_android.cpp), de
 * outra thread -- por isso tudo que mexe com a interface vai por runOnUiThread.
 */
public class AliceActivity extends NativeActivity {
	private static final int REQUEST_PICK_FOLDER = 1;

	// a NativeActivity carrega a .so por conta propria (dlopen), o que nao
	// registra os metodos "native" desta classe no JNI; carregar tambem pelo
	// System.loadLibrary resolve (e a mesma .so, nao carrega duas vezes)
	static {
		System.loadLibrary("Alice");
	}

	private static native void nativeOnGameFolderPicked(String path);

	private boolean waitingForAllFilesAccess = false;
	private AlertDialog folderDialog = null;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
	}

	@Override
	protected void onResume() {
		super.onResume();
		if(waitingForAllFilesAccess) {
			waitingForAllFilesAccess = false;
			if(Environment.isExternalStorageManager()) {
				launchFolderPicker();
			} else {
				showFolderDialog("Sem a permissao \"Acesso a todos os arquivos\" o jogo nao consegue ler a pasta do Victoria 2.");
			}
		}
	}

	/** Chamado pelo C++ quando nao ha pasta do jogo valida configurada. */
	public void requestGameFolder(final String reason) {
		runOnUiThread(() -> showFolderDialog(reason));
	}

	/** Mensagem curta na tela (ex.: "Criando o cenario..."). */
	public void showMessage(final String message) {
		runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_LONG).show());
	}

	private void showFolderDialog(String reason) {
		if(folderDialog != null && folderDialog.isShowing())
			folderDialog.dismiss();
		String message = "O Project Alice precisa dos arquivos do Victoria 2 (com as expansoes).\n\n"
			+ "Copie a pasta do jogo para o aparelho e escolha-a na proxima tela "
			+ "(a pasta que contem \"common\", \"map\" e \"gfx\").";
		if(reason != null && !reason.isEmpty())
			message = reason + "\n\n" + message;
		folderDialog = new AlertDialog.Builder(this)
			.setTitle("Pasta do Victoria 2")
			.setMessage(message)
			.setCancelable(false)
			.setPositiveButton("Escolher pasta", (d, w) -> ensureAccessThenPick())
			.setNegativeButton("Sair", (d, w) -> finish())
			.show();
	}

	private void ensureAccessThenPick() {
		// minSdk 31: o acesso direto (caminhos de arquivo, que o C++ usa) a
		// pastas fora do app exige "Acesso a todos os arquivos"
		if(Environment.isExternalStorageManager()) {
			launchFolderPicker();
			return;
		}
		waitingForAllFilesAccess = true;
		try {
			startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
				Uri.parse("package:" + getPackageName())));
		} catch(Exception e) {
			startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
		}
	}

	private void launchFolderPicker() {
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
		try {
			startActivityForResult(intent, REQUEST_PICK_FOLDER);
		} catch(Exception e) {
			showFolderDialog("Nao foi possivel abrir o seletor de pastas deste aparelho.");
		}
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if(requestCode != REQUEST_PICK_FOLDER)
			return;
		if(resultCode != RESULT_OK || data == null || data.getData() == null) {
			showFolderDialog("Nenhuma pasta foi escolhida.");
			return;
		}
		String path = treeUriToPath(data.getData());
		if(path == null) {
			showFolderDialog("Escolha uma pasta do armazenamento do aparelho ou do cartao SD (nao de um servico na nuvem).");
			return;
		}
		nativeOnGameFolderPicked(path);
	}

	/**
	 * content://com.android.externalstorage.documents/tree/primary%3AGames%2FVictoria%202
	 *   -> /storage/emulated/0/Games/Victoria 2
	 * Cartao SD ("XXXX-XXXX:pasta") -> /storage/XXXX-XXXX/pasta
	 */
	static String treeUriToPath(Uri uri) {
		if(!"com.android.externalstorage.documents".equals(uri.getAuthority()))
			return null;
		String docId;
		try {
			docId = DocumentsContract.getTreeDocumentId(uri);
		} catch(Exception e) {
			return null;
		}
		int colon = docId.indexOf(':');
		if(colon < 0)
			return null;
		String volume = docId.substring(0, colon);
		String relative = docId.substring(colon + 1);
		String base = "primary".equalsIgnoreCase(volume)
			? Environment.getExternalStorageDirectory().getAbsolutePath()
			: "/storage/" + volume;
		return relative.isEmpty() ? base : base + "/" + relative;
	}
}
