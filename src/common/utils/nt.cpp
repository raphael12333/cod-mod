#include "nt.hpp"

namespace utils::nt
{
	library library::load(const std::string& name)
	{
		return library(LoadLibraryA(name.data()));
	}

	library library::get_by_address(void* address)
	{
		HMODULE handle = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, static_cast<LPCSTR>(address), &handle);
		return library(handle);
	}

	library::library()
	{
		this->module_ = GetModuleHandleA(nullptr);
	}

	library::library(const std::string& name)
	{
		this->module_ = GetModuleHandleA(name.data());
	}

	library::library(const HMODULE handle)
	{
		this->module_ = handle;
	}

	bool library::operator==(const library& obj) const
	{
		return this->module_ == obj.module_;
	}

	library::operator bool() const
	{
		return this->is_valid();
	}

	library::operator HMODULE() const
	{
		return this->get_handle();
	}

	PIMAGE_NT_HEADERS library::get_nt_headers() const
	{
		if (!this->is_valid()) return nullptr;
		return reinterpret_cast<PIMAGE_NT_HEADERS>(this->get_ptr() + this->get_dos_header()->e_lfanew);
	}

	PIMAGE_DOS_HEADER library::get_dos_header() const
	{
		return reinterpret_cast<PIMAGE_DOS_HEADER>(this->get_ptr());
	}

	PIMAGE_OPTIONAL_HEADER library::get_optional_header() const
	{
		if (!this->is_valid()) return nullptr;
		return &this->get_nt_headers()->OptionalHeader;
	}

	std::vector<PIMAGE_SECTION_HEADER> library::get_section_headers() const
	{
		std::vector<PIMAGE_SECTION_HEADER> headers;

		auto nt_headers = this->get_nt_headers();
		auto section = IMAGE_FIRST_SECTION(nt_headers);

		for (uint16_t i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i, ++section)
		{
			if (section) headers.push_back(section);
		}

		return headers;
	}

	std::uint8_t* library::get_ptr() const
	{
		return reinterpret_cast<std::uint8_t*>(this->module_);
	}

	void library::unprotect() const
	{
		if (!this->is_valid()) return;

		DWORD protection;
		VirtualProtect(this->get_ptr(), this->get_optional_header()->SizeOfImage, PAGE_EXECUTE_READWRITE, &protection);
	}

	size_t library::get_relative_entry_point() const
	{
		if (!this->is_valid()) return 0;
		return this->get_nt_headers()->OptionalHeader.AddressOfEntryPoint;
	}

	void* library::get_entry_point() const
	{
		if (!this->is_valid()) return nullptr;
		return this->get_ptr() + this->get_relative_entry_point();
	}

	bool library::is_valid() const
	{
		return this->module_ != nullptr && this->get_dos_header()->e_magic == IMAGE_DOS_SIGNATURE;
	}

	std::string library::get_name() const
	{
		if (!this->is_valid()) return {};

		auto path = this->get_path();
		const auto pos = path.find_last_of("/\\");
		if (pos == std::string::npos) return path;

		return path.substr(pos + 1);
	}

	std::string library::get_path() const
	{
		if (!this->is_valid()) return {};

		char name[MAX_PATH]{};
		GetModuleFileNameA(this->module_, name, sizeof name);

		return name;
	}

	std::string library::get_folder() const
	{
		if (!this->is_valid()) return {};

		const auto path = std::filesystem::path(this->get_path());
		return path.parent_path().generic_string();
	}

	void library::free()
	{
		if (this->is_valid())
		{
			FreeLibrary(this->module_);
			this->module_ = nullptr;
		}
	}

	HMODULE library::get_handle() const
	{
		return this->module_;
	}

	void raise_hard_exception()
	{
		int data = false;
		const library ntdll("ntdll.dll");
		ntdll.invoke_pascal<void>("RtlAdjustPrivilege", 19, true, false, &data);
		ntdll.invoke_pascal<void>("NtRaiseHardError", 0xC000007B, 0, nullptr, nullptr, 6, &data);
	}

	std::string load_resource(const int id)
	{
		auto* const res = FindResource(library(), MAKEINTRESOURCE(id), RT_RCDATA);
		if (!res) return {};

		auto* const handle = LoadResource(nullptr, res);
		if (!handle) return {};

		return std::string(LPSTR(LockResource(handle)), SizeofResource(nullptr, res));
	}

	void terminate(const uint32_t code)
	{
		TerminateProcess(GetCurrentProcess(), code);
	}
}