// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include "DriverD3D.h"
#include "Common/TypedConstantBuffer.h"

// forward declarations
class CShader;

namespace gpu
{

struct CounterReadbackEmpty
{
	int Readback(ID3D11UnorderedAccessView* uav)
	{
		assert(0);
		return 0;
	}
};

struct CounterReadbackUsed
{
	CounterReadbackUsed();
	void Readback(ID3D11UnorderedAccessView* uav);
	int  Retrieve();

private:
	D3DBuffer* m_countReadbackBuffer;
#ifdef DURANGO
	void*      m_basePtr;
#endif
	bool       m_readbackCalled;
};

struct DataReadbackEmpty
{
	DataReadbackEmpty(int size, int stride){};
	const void* Map()
	{
		assert(0);
		return NULL;
	};
	void Unmap() { assert(0); };
};

struct DataReadbackUsed
{
	DataReadbackUsed(int size, int stride);
	void Readback(ID3D11Buffer* buf, uint32 readLength);
	const void* Map(uint32 readLength);
	void Unmap();

private:
	ID3D11Buffer* m_readback;
#ifdef DURANGO
	void*         m_basePtr;
#endif
	uint32        m_stride;
	uint32        m_size;
	bool          m_readbackCalled;
};

struct HostDataEmpty
{
	void   Resize(size_t size) {};
	uint8* Get()               { return nullptr; }
};

struct HostDataUsed
{
	void   Resize(size_t size) { m_data.resize(size); }
	uint8* Get()               { return &m_data[0]; }
private:
	std::vector<uint8> m_data;
};

struct BufferFlagsReadWrite
{
	enum
	{
		flags = DX11BUF_STRUCTURED | DX11BUF_BIND_SRV | DX11BUF_BIND_UAV
	};
	typedef CounterReadbackEmpty CounterReadback;
	typedef DataReadbackEmpty    DataReadback;
	typedef HostDataEmpty        HostData;
};

struct BufferFlagsReadWriteReadback
{
	enum
	{
		flags = DX11BUF_STRUCTURED | DX11BUF_BIND_SRV | DX11BUF_BIND_UAV | DX11BUF_UAV_OVERLAP
	};
	typedef CounterReadbackEmpty CounterReadback;
	typedef DataReadbackUsed     DataReadback;
	typedef HostDataEmpty        HostData;
};

struct BufferFlagsReadWriteAppend
{
	enum
	{
		flags = DX11BUF_STRUCTURED | DX11BUF_BIND_SRV | DX11BUF_BIND_UAV | DX11BUF_UAV_COUNTER
	};
	typedef CounterReadbackUsed CounterReadback;
	typedef DataReadbackEmpty   DataReadback;
	typedef HostDataEmpty       HostData;
};

struct BufferFlagsDynamic
{
	enum
	{
		flags = DX11BUF_STRUCTURED | DX11BUF_BIND_SRV | DX11BUF_DYNAMIC
	};
	typedef CounterReadbackEmpty CounterReadback;
	typedef DataReadbackEmpty    DataReadback;
	typedef HostDataUsed         HostData;
};

struct BufferFlagsDynamicTyped
{
	enum
	{
		flags = DX11BUF_BIND_SRV | DX11BUF_DYNAMIC
	};
	typedef CounterReadbackEmpty CounterReadback;
	typedef DataReadbackEmpty    DataReadback;
	typedef HostDataUsed         HostData;
};

struct BufferFlagsReadWriteTyped
{
	enum
	{
		flags = DX11BUF_BIND_SRV | DX11BUF_BIND_UAV
	};
	typedef CounterReadbackEmpty CounterReadback;
	typedef DataReadbackEmpty    DataReadback;
	typedef HostDataEmpty        HostData;
};

template<typename BFlags> class CStridedResource
{
public:
	CStridedResource(int stride, int size)
		: m_size(size), m_stride(stride), m_dataReadback(size, stride)
	{
		m_buffer.Create(size, stride, DXGI_FORMAT_UNKNOWN, BFlags::flags, NULL);
	}
	ID3D11UnorderedAccessView* GetUAV()    { return m_buffer.GetDeviceUAV(); };
	ID3D11ShaderResourceView*  GetSRV()    { return m_buffer.GetSRV(); };
	ID3D11Buffer*              GetBuffer() { return m_buffer.GetBuffer(); };

	void                       UpdateBufferContent(void* pData, size_t nSize)
	{
		m_buffer.UpdateBufferContent(pData, m_stride * nSize);
	};
	void        ReadbackCounter() { return m_counterReadback.Readback(m_buffer.GetDeviceUAV()); };
	int         RetrieveCounter() { return m_counterReadback.Retrieve(); };
	void        Readback()        { return m_dataReadback.Readback(m_buffer.GetBuffer()); };
	const void* Map()             { return (const void*)m_dataReadback.Map(); };
	void        Unmap()           { return m_dataReadback.Unmap(); };

private:
	const int  m_size;
	const int  m_stride;
	CGpuBuffer m_buffer;
	// this is only an nonempty struct if it is used
	typedef typename BFlags::CounterReadback CounterReadback;
	CounterReadback m_counterReadback;
	typedef typename BFlags::DataReadback    DataReadback;
	DataReadback    m_dataReadback;
};

template<typename T, typename BFlags> class CTypedResource
{
public:
	CTypedResource(int size) : m_size(size), m_dataReadback(size, sizeof(T))
	{
		m_hostData.Resize(size * sizeof(T));
	}

	void CreateDeviceBuffer()
	{
		m_buffer.Create(m_size, sizeof(T), DXGI_FORMAT_UNKNOWN, BFlags::flags, NULL);
	}
	void FreeDeviceBuffer()
	{
		m_buffer.Release();
	}
	CGpuBuffer& GetBuffer() { return m_buffer; };

	T&          operator[](size_t i)
	{
		return *reinterpret_cast<T*>(m_hostData.Get() + i * sizeof(T));
	}
	size_t GetSize() const { return m_size; }
	void   UploadHostData()
	{
		UpdateBufferContent(m_hostData.Get(), m_size);
	};
	void UpdateBufferContent(void* pData, size_t nSize)
	{
		m_buffer.UpdateBufferContent(pData, sizeof(T) * nSize);
	};
	void UpdateBufferContentAligned(void* pData, size_t nSize)
	{
		m_buffer.UpdateBufferContent(pData, Align(sizeof(T) * nSize, CRY_PLATFORM_ALIGNMENT));
	};
	void     ReadbackCounter()           { return m_counterReadback.Readback(m_buffer.GetDeviceUAV()); };
	int      RetrieveCounter()           { return m_counterReadback.Retrieve(); };
	void     Readback(uint32 readLength) { return m_dataReadback.Readback(m_buffer.GetBuffer(), readLength); };
	const T* Map(uint32 readLength)      { return (const T*)m_dataReadback.Map(readLength); };
	void     Unmap()                     { return m_dataReadback.Unmap(); };
	bool     IsDeviceBufferAllocated()   { return m_buffer.GetBuffer() != nullptr; }
private:
	const int  m_size;
	CGpuBuffer m_buffer;
	// this is only an nonempty struct if it is used
	typedef typename BFlags::CounterReadback CounterReadback;
	CounterReadback m_counterReadback;
	typedef typename BFlags::DataReadback    DataReadback;
	DataReadback    m_dataReadback;
	typedef typename BFlags::HostData        HostData;
	HostData        m_hostData;
};

template<typename T> class CTypedConstantBuffer : public ::CTypedConstantBuffer<T>
{
	typedef typename ::CTypedConstantBuffer<T> TBase;
public:
	bool IsDeviceBufferAllocated() { return TBase::m_constantBuffer != nullptr; }
	T&   operator=(const T& hostData)
	{
		return TBase::m_hostBuffer = hostData;
	}
	T&       GetHostData()       { return TBase::m_hostBuffer; }
	const T& GetHostData() const { return TBase::m_hostBuffer; }
};

inline int GetNumberOfBlocksForArbitaryNumberOfThreads(const int threads, const int blockSize)
{
	return threads / blockSize + (threads % blockSize != 0);
}

template<class T>
class CDoubleBuffered
{
public:
	CDoubleBuffered(int size) : m_size(size), m_current(0), m_isDoubleBuffered(false) {}
	void Initialize(bool isDoubleBuffered)
	{
		Reset();

		m_buffers[0] = std::unique_ptr<T>(new T(m_size));
		m_buffers[0]->CreateDeviceBuffer();

		if (isDoubleBuffered)
		{
			m_buffers[1] = std::unique_ptr<T>(new T(m_size));
			m_buffers[1]->CreateDeviceBuffer();
		}

		m_isDoubleBuffered = isDoubleBuffered;
	}
	void Reset()
	{
		m_current = 0;
		m_buffers[0].reset();
		m_buffers[1].reset();
	}
	T&       Get()                 { return *m_buffers[m_current]; }
	const T& Get() const           { return *m_buffers[m_current]; }
	T&       GetBackBuffer()       { return *m_buffers[!m_current]; }
	const T& GetBackBuffer() const { return *m_buffers[!m_current]; }
	void     Swap()
	{
		if (m_isDoubleBuffered)
			m_current = !m_current;
	}
	int GetCurrentBufferId() const { return m_current; }

private:
	std::unique_ptr<T> m_buffers[2];
	const int          m_size;
	int                m_current;
	bool               m_isDoubleBuffered;
};

}
